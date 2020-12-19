// Axel '0vercl0k' Souchet - December 1 2020
#include "fmt/format.h"
#include "fmt/os.h"
#include "pagetables.h"
#include <cmath>
#include <filesystem>

//
// Turn this on to dump the gap mappings.
//

constexpr bool VerboseDumpGapMappings = false;

//
// Turn this on to dump the VA mappings.
//

constexpr bool VerboseDumpMappings = false;

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

  struct Region_t {
    uint64_t Va = 0;
    uint64_t EndIdx = 0;
  };

  //
  // The tape is basically a succession of page table properties. This is used
  // as distances on the curve.
  //

  std::vector<ptables::Protection_t> Tape_;

  //
  // Keep track of contiguous regions of memory.
  //

  std::vector<Region_t> Regions_;

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

  bool Parse(const kdmpparser::KernelDumpParser &DumpParser,
             const uint64_t DirectoryBase) {

    //
    // Initialize the page tables walker.
    //

    ptables::PageTableWalker_t Walker(DumpParser, DirectoryBase);

    //
    // Warm up the tape.
    //

    Tape_.reserve(1'500'000);

    //
    // Let's go!
    //

    uint64_t LastVa = 0;
    Region_t Region;
    while (1) {

      //
      // Grab an entry from the iterator.
      //

      const auto &Entry = Walker.Next();

      //
      // If there's no entry, the page tables walk is done.
      //

      if (!Entry) {

        //
        // When we're done, complete the segment.
        //

        Region.EndIdx = Tape_.size() - 1;
        Regions_.emplace_back(Region);
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
        bool Gap = false;

        //
        // If we haven't added anything to the tape, it means the address space
        // starts with a gap. So it means that the first page is a gap and as a
        // result we should not increase the last virtual address.
        //

        if (Tape_.size() > 0) {
          LastVa += page::Size;
        }

        for (; LastVa != Entry->Va; LastVa += page::Size) {
          if constexpr (VerboseDumpGapMappings) {
            fmt::print("VA:{:#x} (Gap, Dist:{})\n", LastVa, Tape_.size());
          }

          Tape_.emplace_back(ptables::Protection_t::None);

          //
          // If we exhausted the number of allowed consecutive entries, we break
          // out of the loop.
          //

          if (N++ >= MaxGapEntries) {
            if constexpr (VerboseDumpMappings) {
              fmt::print("Huge gap from {:x} to {:x}, skipping\n", LastVa,
                         Entry->Va);
            }

            Gap = true;
            break;
          }
        }

        if (Gap) {

          //
          // If we have a gap, close the current segment and start
          // a new one.
          //

          Region.EndIdx = Tape_.size();
          Regions_.emplace_back(Region);

          //
          // Start a new one.
          //

          Region.Va = Entry->Va;
        }
      }

      //
      // Calculate the page protection from the PML4E/PDPTE/PDE/PTE.
      //

      const auto &Protection = Entry->Protection();

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
          fmt::print("VA:{:#x}, PA:{:#x} ({}, {}, PML4E:{:#x}, PDPTE:{:#x}, "
                     "PDE:{:#x}, PTE:{:#x}, Dist:{})\n",
                     CurrentVa, CurrentPa, ToString(Protection),
                     ToString(Entry->Type), Entry->Pml4eAddress,
                     Entry->PdpteAddress, Entry->PdeAddress, Entry->PteAddress,
                     Tape_.size());
        }

        //
        // Emplace the properties.
        //

        Tape_.emplace_back(Protection);
      }
    }

    //
    // We're done.
    //

    fmt::print("Extracted {} properties from the dump and {} contiguous "
               "regions\n",
               Tape_.size(), Regions_.size());
    return true;
  }

  //
  // Write the tape on the disk with the PPM format.
  //

  bool Write(const fs::path &Filename) const {
    const uint64_t Log2 = std::log2(float(Tape_.size()));
    const uint64_t Order = (Log2 / 2);
    const uint64_t Width = uint64_t(1) << Order;
    const uint64_t Height = Width;
    fmt::print("Laying it out on an hilbert-curve order {} ({} total pixels)\n",
               Order, Width * Height);

    auto File = fmt::output_file(Filename.string());
    File.print("{} {}\n", Width, Height);
    uint64_t Idx = 0;
    for (const auto &Region : Regions_) {
      File.print("{:#x}\n", Region.Va);
      for (; Idx != Region.EndIdx; Idx++) {
        File.print("{:x}\n", Tape_[Idx]);
      }
    }

    return true;
  }
};
} // namespace clairvoyance

int main(int argc, char *argv[]) {
  if (argc < 2) {
    fmt::print("./clairvoyance <dump path> [<page dir pa>]\n");
    return 0;
  }

  //
  // Parse the dump file.
  //

  const fs::path DumpFile(argv[1]);
  kdmpparser::KernelDumpParser DumpParser;
  if (!DumpParser.Parse(DumpFile.string().c_str())) {
    fmt::print("Parse failed\n");
    return false;
  }

  //
  // Warn if there is a chance to not have the full page tables hierarchy in
  // the dump.
  //

  if (DumpParser.GetDumpType() != kdmpparser::DumpType_t::FullDump) {
    fmt::print("/!\\ {} is not a full dump so some pages might be missing\n",
               DumpFile.string());
  }

  //
  // Get the default @cr3 if one is not specified from the user.
  //

  const uint64_t DirectoryBase = [&]() {
    if (argc == 3) {
      return uint64_t(strtoull(argv[2], nullptr, 0));
    }

    return DumpParser.GetDirectoryTableBase();
  }();

  //
  // Check that the PML4 at least exists.
  //

  if (DumpParser.GetPhysicalPage(DirectoryBase) == nullptr) {
    fmt::print("The page directory {:#x} is not mapped in the dump file\n",
               DirectoryBase);
    return 0;
  }

  //
  // Parse the dump and prepare the curve.
  //

  clairvoyance::Visualizer_t Visu;
  if (!Visu.Parse(DumpParser, DirectoryBase)) {
    fmt::print("Parse failed\n");
    return 0;
  }

  //
  // Write the picture on disk.
  //

  const auto &Filename = fmt::format("{}-{:#x}.clairvoyance",
                                     DumpFile.stem().string(), DirectoryBase);
  const fs::path OutFile(fs::current_path() / Filename);
  if (!Visu.Write(OutFile)) {
    fmt::print("Write failed\n");
    return 0;
  }

  //
  // Yay!
  //

  fmt::print("Done writing {}\n", Filename);
  return 1;
}