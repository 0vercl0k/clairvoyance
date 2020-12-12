// Axel '0vercl0k' Souchet - December 1 2020
#include "fmt/format.h"
#include "fmt/os.h"
#include "hilbert.h"
#include "pagetables.h"
#include <cmath>
#include <filesystem>

//
// Turn this on to dump the VA mappings.
//

constexpr bool VerboseDumpMappings = true;

namespace fs = std::filesystem;

namespace clairvoyance {

//
// The palette of color used for the visualization.
//

namespace color {
constexpr uint32_t White = 0xff'ff'ff;
constexpr uint32_t Black = 0x00'00'00;
constexpr uint32_t Green = 0x00'ff'00;
constexpr uint32_t PaleGreen = 0xa9'ff'52;
constexpr uint32_t CanaryYellow = 0xff'ff'99;
constexpr uint32_t Yellow = 0xff'ff'00;
constexpr uint32_t Purple = 0xa0'20'f0;
constexpr uint32_t Mauve = 0xe0'b0'ff;
constexpr uint32_t Red = 0xfe'00'00;
constexpr uint32_t LightRed = 0xff'7f'7f;
}; // namespace color

//
// The visualizer is the class that generates the pictures. It reads the dump,
// parses the page tables hierarchy and puts the address space onto a hilbert
// curve.
//

class Visualizer_t {

  //
  // The tape is basically a succession of page table properties. This is used
  // as distances on the curve.
  //

  std::vector<ptables::Properties_t> Tape_;

  //
  // Kernel dump parser.
  //

  kdmpparser::KernelDumpParser DumpParser_;

  //
  // Gets the associated color for specific page properties.
  //

  static constexpr uint32_t PropertiesToRgb(const ptables::Properties_t &Prop) {
    //
    // XXX: Use the below once clang & gcc supports it.
    // using enum ptables::Properties_t;
    //

    switch (Prop) {
    case ptables::Properties_t::None:
      return color::Black;
    case ptables::Properties_t::UserRead:
      return color::PaleGreen;
    case ptables::Properties_t::UserReadExec:
      return color::CanaryYellow;
    case ptables::Properties_t::UserReadWrite:
      return color::Mauve;
    case ptables::Properties_t::UserReadWriteExec:
      return color::LightRed;
    case ptables::Properties_t::KernelRead:
      return color::Green;
    case ptables::Properties_t::KernelReadExec:
      return color::Yellow;
    case ptables::Properties_t::KernelReadWrite:
      return color::Purple;
    case ptables::Properties_t::KernelReadWriteExec:
      return color::Red;
    }

    std::abort();
  }

  //
  // Gets the number of 4k pages that we need to draw on the curve for
  // Huge/Large/Normal page.
  //

  static constexpr uint64_t
  GetNumberPixels(const ptables::PageType_t PageType) {
    //
    // XXX: Use the below once clang and gcc supports the feature.
    // using enum ptables::PageType_t;
    //

    switch (PageType) {
    case ptables::PageType_t::Huge: {

      //
      // Huge pages are 1GB so we'll add the below number of pixels.
      //

      return (1024 * 1024 * 1024) / page::Size;
    }

    case ptables::PageType_t::Large: {

      //
      // Large pages are 2MB so we'll add the below number of pixels.
      //

      return (1024 * 1024 * 2) / page::Size;
    }

    case ptables::PageType_t::Normal: {

      //
      // This is a normal page, so we'll just materialize a single pixel.
      //

      return 1;
    }
    }

    std::abort();
  }

public:
  //
  // Parses and prepares the tape.
  //

  bool Parse(const fs::path &DumpFile) {

    //
    // Parse the dump file.
    //

    if (!DumpParser_.Parse(DumpFile.string().c_str())) {
      fmt::print("Parse failed\n");
      return false;
    }

    //
    // Warn if there is a chance to not have the full page tables hierarchy in
    // the dump.
    //

    if (DumpParser_.GetDumpType() != kdmpparser::DumpType_t::FullDump) {
      fmt::print("/!\\ {} is not a full dump so some pages might be missing\n",
                 DumpFile.filename().string());
    }

    //
    // Initialize the page tables iterator with the current @cr3.
    //

    const uint64_t DirectoryBase = DumpParser_.GetDirectoryTableBase();
    ptables::PageTableWalker_t Walker(DumpParser_, DirectoryBase);

    //
    // Warm up the tape.
    //

    Tape_.reserve(500'000);

    //
    // Let's go!
    //

    uint64_t LastVa = 0;
    while (1) {

      //
      // Grab an entry from the iterator.
      //

      const auto &Entry = Walker.Next();

      //
      // If there's no entry, the page tables walk is done.
      //

      if (!Entry) {
        break;
      }

      //
      // If we have a gap in the address space, we fill it up the best we can.
      //

      if ((LastVa + page::Size) != Entry->Va) {

        //
        // Fill the gap with at most 10k entries of black pixels.
        //

        uint64_t N = 0;
        constexpr uint64_t MaxGapEntries = 10'000;
        for (uint64_t CurLastVa = LastVa; (CurLastVa + page::Size) < Entry->Va;
             CurLastVa += page::Size) {
          Tape_.emplace_back(ptables::Properties_t::None);

          //
          // If we had enough consecutive entries, we break out of the loop.
          //

          if (N++ >= MaxGapEntries) {
            fmt::print("Huge gap from {:x} to {:x}, skipping\n", LastVa,
                       Entry->Va);
            break;
          }
        }
      }

      //
      // Calculate the page properties from the PML4E/PDPTE/PDE/PTE.
      //

      const auto &Properties = Entry->Properties();

      //
      // Grab the number of pixels that we need for this page.
      //

      const uint64_t NumberPixels = GetNumberPixels(Entry->Type);

      //
      // Time to populate the tape.
      //

      for (uint64_t Idx = 0; Idx < NumberPixels; Idx++) {
        const uint64_t CurrentPa = ptables::AddressFromPfn(Entry->Pa, Idx);
        const uint64_t CurrentVa = ptables::AddressFromPfn(Entry->Va, Idx);
        LastVa = CurrentVa;

        //
        // Dump the mappings if the user want to.
        //

        if (VerboseDumpMappings) {
          fmt::print("VA:{:#x} ({}, {}) PA:{:#x}\n", CurrentVa,
                     PropertiesToString(Properties),
                     Entry->Type != ptables::PageType_t::Normal ? ">4k" : "4k",
                     CurrentPa);
        }

        //
        // Emplace the properties.
        //

        Tape_.emplace_back(Properties);
      }
    }

    //
    // We're done.
    //

    fmt::print("Extracted {} properties from the dump file\n", Tape_.size());
    return true;
  }

  //
  // Write the tape on the disk with the PPM format.
  //

  bool Write(const std::string_view &Filename) const {
    const uint64_t Log2 = std::log2(float(Tape_.size()));
    const uint64_t Order = (Log2 / 2);
    const uint64_t Height = hilbert::Height(Order);
    const uint64_t Width = hilbert::Width(Order);

    fmt::print("Laying it out on an hilbert-curve order {} ({} total pixels)\n",
               Order, Width * Height);

    auto File = fmt::output_file(Filename.data());
    File.print("P3\n{} {}\n255\n", Width, Height);
    for (uint64_t Y = 0; Y < Height; Y++) {
      for (uint64_t X = 0; X < Width; X++) {
        const auto &Distance = hilbert::DistanceFromCoordinates(X, Y, Order);
        const uint32_t Rgb = (Distance < Tape_.size())
                                 ? PropertiesToRgb(Tape_[Distance])
                                 : color::White;
        File.print("{} {} {}\n", (Rgb >> 16) & 0xff, (Rgb >> 8) & 0xff,
                   (Rgb >> 0) & 0xff);
      }
      File.print("\n");
    }

    return true;
  }
};

} // namespace clairvoyance

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fmt::print("./clairvoyance <dmp>\n");
    return 0;
  }

  //
  // Parse the dump and prepare the curve.
  //

  const fs::path DumpFile(argv[1]);
  clairvoyance::Visualizer_t Visu;
  if (!Visu.Parse(DumpFile)) {
    fmt::print("Parse failed\n");
    return 0;
  }

  //
  // Write the picture on disk.
  //

  if (!Visu.Write("vis.ppm")) {
    fmt::print("Write failed\n");
    return 0;
  }

  //
  // Yay!
  //

  fmt::print("Done\n");
  return 1;
}