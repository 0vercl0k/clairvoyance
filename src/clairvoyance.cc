// Axel '0vercl0k' Souchet - December 1 2020
#include "fmt/format.h"
#include "fmt/os.h"
#include "hilbert.h"
#include "pagetables.h"
#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

namespace clairvoyance {
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

constexpr uint32_t PropertiesToRgb(const ptables::Properties_t &Prop) {
  using enum ptables::Properties_t;
  switch (Prop) {
  case None:
    return color::Black;
  case UserRead:
    return color::PaleGreen;
  case UserReadExec:
    return color::CanaryYellow;
  case UserReadWrite:
    return color::Mauve;
  case UserReadWriteExec:
    return color::LightRed;
  case KernelRead:
    return color::Green;
  case KernelReadExec:
    return color::Yellow;
  case KernelReadWrite:
    return color::Purple;
  case KernelReadWriteExec:
    return color::Red;
  }

  std::abort();
}

static constexpr uint64_t GetNumberPixels(const ptables::PageType_t PageType) {
  using enum ptables::PageType_t;
  switch (PageType) {
  case Huge: {

    //
    // Huge pages are 1GB so we'll add the below number of pixels.
    //

    return (1024 * 1024 * 1024) / page::Size;
  }

  case Large: {

    //
    // Large pages are 2MB so we'll add the below number of pixels.
    //

    return (1024 * 1024 * 2) / page::Size;
  }

  case Normal: {

    //
    // This is a normal page, so we'll just materialize a single pixel.
    //

    return 1;
  }
  }

  std::abort();
}

class Visualizer_t {
  std::vector<ptables::Properties_t> Tape_;
  uint64_t LastVa_ = 0;
  kdmpparser::KernelDumpParser DumpParser_;

public:
  bool Render(const fs::path &DumpFile) {
    if (!DumpParser_.Parse(DumpFile.string().c_str())) {
      fmt::print("Parse failed\n");
      return false;
    }

    if (DumpParser_.GetDumpType() != kdmpparser::DumpType_t::FullDump) {
      fmt::print("/!\\ {} is not a full dump so some pages might be missing\n",
                 DumpFile.filename().string());
    }

    ptables::PageTableIterator_t Iterator(DumpParser_,
                                          DumpParser_.GetDirectoryTableBase());
    Tape_.reserve(500'000);

    uint64_t LastVa = 0;
    while (1) {
      const auto &Entry = Iterator.Next();
      if (!Entry) {
        break;
      }

      if ((LastVa + page::Size) != Entry->Va) {

        //
        // We have a gap, split it in 2mb chunk of black pixels.
        //

        uint64_t N = 0;
        constexpr uint64_t MaxGapEntries = 10'000;
        for (uint64_t CurLastVa = LastVa; (CurLastVa + page::Size) < Entry->Va;
             CurLastVa += page::Size) {
          Tape_.emplace_back(ptables::Properties_t::None);
          N++;
          if (N >= MaxGapEntries) {
            fmt::print("Huge gap from {:x} to {:x}, skipping\n", LastVa,
                       Entry->Va);
            break;
          }
        }
      }

      const auto &Properties = ptables::PropertiesFromPtes(
          Entry->Pml4e, Entry->Pdpte, Entry->Pde, Entry->Pte);
      const uint64_t NumberPixels = GetNumberPixels(Entry->Type);
      for (uint64_t Idx = 0; Idx < NumberPixels; Idx++) {
        const uint64_t CurrentPa = ptables::AddressFromPfn(Entry->Pa, Idx);
        const uint64_t CurrentVa = ptables::AddressFromPfn(Entry->Va, Idx);
        LastVa = CurrentVa;

        // fmt::print("VA:{:#x} ({}, {}) PA:{:#x}\n", CurrentVa,
        //           PropertiesToString(Properties),
        //           Entry->Type != clairvoyance::PageType_t::Normal ? ">4k" :
        //           "4k", CurrentPa);
        Tape_.emplace_back(Properties);
      }
    }

    fmt::print("Extracted {} properties from the dump file\n", Tape_.size());
    return true;
  }

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

  const fs::path DumpFile(argv[1]);
  clairvoyance::Visualizer_t Visu;
  if (!Visu.Render(DumpFile)) {
    fmt::print("Render failed\n");
    return 0;
  }

  if (!Visu.Write("vis.ppm")) {
    fmt::print("Write failed\n");
    return 0;
  }

  fmt::print("Done\n");
  return 1;
}