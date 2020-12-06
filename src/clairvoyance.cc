// Axel '0vercl0k' Souchet - December 1 2020
#include "fmt/format.h"
#include "fmt/os.h"
#include "hilbert.h"
#include "kdmp-parser.h"
#include <filesystem>
#include <optional>
#include <unordered_map>

namespace fs = std::filesystem;

namespace clairvoyance {
using KernelDumpParser_t = kdmpparser::KernelDumpParser;

namespace Page {

//
// Page size.
//

constexpr uint64_t Size = 0x1000;

//
// Page align an address.
//

constexpr uint64_t Align(const uint64_t Address) {
  return Address & ~(Size - 1);
}

//
// Extract the page offset off an address.
//

constexpr uint64_t Offset(const uint64_t Address) {
  return Address & (Size - 1);
}

//
// Get an absolute address from a PFN (with a base).
//

constexpr uint64_t AddressFromPfn(const uint64_t Pfn) { return Pfn * Size; }
constexpr uint64_t AddressFromPfn(const uint64_t Base, const uint64_t Pfn) {
  return Base + (Pfn * Size);
}

//
// Is it a canonical address?
//

constexpr bool Canonical(const uint64_t Va) {
  const uint64_t Upper = Va >> 47ULL;
  return Upper == 0b11111111111111111 || Upper == 0;
}

} // namespace Page

union PteHardware_t {
  struct {
    uint64_t Present : 1;
    uint64_t Write : 1;
    uint64_t UserAccessible : 1;
    uint64_t WriteThrough : 1;
    uint64_t CacheDisable : 1;
    uint64_t Accessed : 1;
    uint64_t Dirty : 1;
    uint64_t LargePage : 1;
    uint64_t Available : 4;
    uint64_t PageFrameNumber : 36;
    uint64_t ReservedForHardware : 4;
    uint64_t ReservedForSoftware : 11;
    uint64_t NoExecute : 1;
  } u;
  uint64_t AsUINT64;
  constexpr PteHardware_t(const uint64_t Value) : AsUINT64(Value) {}
};

static_assert(sizeof(PteHardware_t) == 8);

//
// Structure to parse a virtual address.
//

struct VirtualAddress_t {
  union {
    struct {
      uint64_t Offset : 12;
      uint64_t PtIndex : 9;
      uint64_t PdIndex : 9;
      uint64_t PdptIndex : 9;
      uint64_t Pml4Index : 9;
      uint64_t Reserved : 16;
    } u;
    uint64_t AsUINT64;
  } u;

  constexpr explicit VirtualAddress_t(const uint64_t Value) {
    u.AsUINT64 = Value;
  }

  constexpr explicit VirtualAddress_t(const uint64_t Pml4eIndex,
                                      const uint64_t PdpteIndex,
                                      const uint64_t PdeIndex = 0,
                                      const uint64_t PteIndex = 0) {
    u.AsUINT64 = 0;
    Pml4Index(Pml4eIndex);
    PdptIndex(PdpteIndex);
    PdIndex(PdeIndex);
    PtIndex(PteIndex);
  }

  constexpr uint64_t U64() const { return u.AsUINT64; }
  constexpr uint64_t Offset() const { return u.u.Offset; }
  constexpr void Offset(const uint64_t Offset) { u.u.Offset = Offset; }
  constexpr uint64_t PtIndex() const { return u.u.PtIndex; }
  constexpr void PtIndex(const uint64_t PtIndex) { u.u.PtIndex = PtIndex; }
  constexpr uint64_t PdIndex() const { return u.u.PdIndex; }
  constexpr void PdIndex(const uint64_t PdIndex) { u.u.PdIndex = PdIndex; }
  constexpr uint64_t PdptIndex() const { return u.u.PdptIndex; }
  constexpr void PdptIndex(const uint64_t PdptIndex) {
    u.u.PdptIndex = PdptIndex;
  }
  constexpr uint64_t Pml4Index() const { return u.u.Pml4Index; }
  constexpr void Pml4Index(const uint64_t Pml4Index) {
    u.u.Pml4Index = Pml4Index;
    if ((Pml4Index >> 8) & 1) {
      u.u.Reserved = 0b11111111111111111;
    } else {
      u.u.Reserved = 0;
    }
  }
};

static_assert(sizeof(VirtualAddress_t) == 8);

enum class Properties_t {
  UserRead,
  UserReadExec,
  UserReadWrite,
  UserReadWriteExec,
  KernelRead,
  KernelReadExec,
  KernelReadWrite,
  KernelReadWriteExec
};

constexpr std::string_view PropertiesToString(const Properties_t &Prop) {
  using enum Properties_t;
  switch (Prop) {
  case UserRead:
    return "UserRead";
  case UserReadExec:
    return "UserReadExec";
  case UserReadWrite:
    return "UserReadWrite";
  case UserReadWriteExec:
    return "UserReadWriteExec";
  case KernelRead:
    return "KernelRead";
  case KernelReadExec:
    return "KernelReadExec";
  case KernelReadWrite:
    return "KernelReadWrite";
  case KernelReadWriteExec:
    return "KernelReadWriteExec";
  }

  std::abort();
}

constexpr Properties_t PropertiesFromPtes(const PteHardware_t Pml4e,
                                          const PteHardware_t Pdpte,
                                          const PteHardware_t Pde = 0,
                                          const PteHardware_t Pte = 0) {
  using enum Properties_t;
  auto And = [](const bool A, const bool B) { return A && B; };
  auto Or = [](const bool A, const bool B) { return A || B; };

#define CalculateBit(FieldName, Comp)                                          \
  [&]() {                                                                      \
    bool FieldName = Comp(Pml4e.u.##FieldName, Pdpte.u.##FieldName);           \
    if (!Pdpte.u.LargePage) {                                                  \
      FieldName = Comp(FieldName, Pde.u.##FieldName);                          \
    }                                                                          \
    if (!Pde.u.LargePage) {                                                    \
      FieldName = Comp(FieldName, Pte.u.##FieldName);                          \
    }                                                                          \
    return FieldName;                                                          \
  }();

  const bool UserAccessible = CalculateBit(UserAccessible, And);
  const bool Write = CalculateBit(Write, And);
  const bool NoExecute = CalculateBit(NoExecute, Or);

  if (UserAccessible) {
    if (Write) {
      if (NoExecute) {
        return UserReadWrite;
      } else {
        return UserReadWriteExec;
      }
    } else {
      if (NoExecute) {
        return UserRead;
      } else {
        return UserReadExec;
      }
    }
  }

  if (Write) {
    if (NoExecute) {
      return KernelReadWrite;
    } else {
      return KernelReadWriteExec;
    }
  } else {
    if (NoExecute) {
      return KernelRead;
    } else {
      return KernelReadExec;
    }
  }
  std::abort();
} // namespace clairvoyance

enum class Color_t { Black, White, Red };

class DumpColor_t {
public:
private:
  KernelDumpParser_t DumpParser_;
  std::vector<Color_t> Colors_;
  std::unordered_multimap<uint64_t, uint64_t> Reverse_;

  static std::string PageTableBitsToString(const PteHardware_t &Entry) {
    std::string S("-----------");
    S[2] = Entry.u.LargePage ? 'L' : '-';
    S[3] = Entry.u.Dirty ? 'D' : '-';
    S[4] = Entry.u.Accessed ? 'A' : '-';

    S[7] = Entry.u.UserAccessible ? 'U' : 'K';
    S[8] = Entry.u.Write ? 'W' : 'R';
    S[9] = Entry.u.NoExecute ? '-' : 'E';
    S[10] = Entry.u.Present ? 'V' : '-';
    return S;
  }

  enum class PageType_t { HugePage, LargePage, NormalPage };
  constexpr uint64_t GetNumberPixels(const PageType_t PageType) {
    switch (PageType) {
    case PageType_t::HugePage: {

      //
      // Huge pages are 1GB so we'll add the below number of pixels.
      //

      return (1024 * 1024 * 1024) / Page::Size;
    }

    case PageType_t::LargePage: {

      //
      // Large pages are 2MB so we'll add the below number of pixels.
      //

      return (1024 * 1024 * 2) / Page::Size;
    }

    case PageType_t::NormalPage: {

      //
      // This is a normal page, so we'll just materialize a single pixel.
      //

      return 1;
    }
    }

    std::abort();
  }

  void AddPage(const VirtualAddress_t &Va, const PteHardware_t Pml4e,
               const PteHardware_t Pdpte, const PteHardware_t Pde = 0,
               const PteHardware_t Pte = 0) {
    PageType_t PageType = PageType_t::NormalPage;
    uint64_t Pa = 0;
    if (Pdpte.u.LargePage) {
      PageType = PageType_t::HugePage;
      Pa = Page::AddressFromPfn(Pdpte.u.PageFrameNumber);
    } else if (Pde.u.LargePage) {
      PageType = PageType_t::LargePage;
      Pa = Page::AddressFromPfn(Pde.u.PageFrameNumber);
    } else {
      Pa = Page::AddressFromPfn(Pte.u.PageFrameNumber);
    }

    const uint64_t NumberPixels = GetNumberPixels(PageType);
    const auto Properties = PropertiesFromPtes(Pml4e, Pdpte, Pde, Pte);
    for (uint64_t Idx = 0; Idx < NumberPixels; Idx++) {
      const uint64_t CurrentPa = Page::AddressFromPfn(Pa, Idx);
      const uint64_t CurrentVa = Page::AddressFromPfn(Va.U64(), Idx);
      if (CurrentVa == 0xffffdf8000369000) {
        fmt::print("{:x}: {:x} {:x} {:x} {:x}\n", CurrentVa, Pml4e.AsUINT64,
                   Pdpte.AsUINT64, Pde.AsUINT64, Pte.AsUINT64);
        __debugbreak();
      }

      if (Reverse_.count(CurrentPa) >= 10) {
        continue;
      }

      fmt::print("VA:{:#x} ({}, {}) PA:{:#x}\n", CurrentVa,
                 PropertiesToString(Properties),
                 PageType != PageType_t::NormalPage ? ">4k" : "4k", CurrentPa);
      Colors_.emplace_back(Color_t::Red);
      Reverse_.emplace(CurrentPa, CurrentVa);
    }
  }

public:
  bool ComputePixels(const fs::path &DumpFile) {
    if (!fs::exists(DumpFile)) {
      fmt::print("{} does not exist on the filesystem.\n", DumpFile.string());
      return false;
    }

    if (!DumpParser_.Parse(DumpFile.string().c_str())) {
      fmt::print("Parse failed\n");
      return false;
    }

    if (DumpParser_.GetDumpType() != kdmpparser::DumpType_t::FullDump) {
      fmt::print("/!\\ {} is not a full dump so some of the page table "
                 "hierarchy will be missing\n",
                 DumpFile.string());
    }

    constexpr uint64_t NumberEntries = (Page::Size / sizeof(uint64_t));
    const uint64_t Cr3 = DumpParser_.GetDirectoryTableBase();
    const auto Pml4 = (PteHardware_t *)DumpParser_.GetPhysicalPage(Cr3);

    if (Pml4 == nullptr) {
      fmt::print("PML4:{:#x} not available in the dump, bailing\n", Cr3);
      return false;
    }

    for (uint64_t Pml4Idx = 0; Pml4Idx < NumberEntries; Pml4Idx++) {
      const auto &Pml4e = Pml4[Pml4Idx];
      if (!Pml4e.u.Present) {
        continue;
      }

      const uint64_t Pml4eAddress = Cr3 + (Pml4Idx * sizeof(uint64_t));
      const uint64_t PdptAddress =
          Page::AddressFromPfn(Pml4e.u.PageFrameNumber);
      const auto Pdpt =
          (PteHardware_t *)DumpParser_.GetPhysicalPage(PdptAddress);

      if (Pdpt == nullptr) {
        fmt::print("PDPT:{:#x} not available in the dump, bailing\n",
                   PdptAddress);
        continue;
      }

      for (uint64_t PdptIdx = 0; PdptIdx < NumberEntries; PdptIdx++) {
        const auto &Pdpte = Pdpt[PdptIdx];
        if (!Pdpte.u.Present) {
          continue;
        }

        if (Pdpte.u.LargePage) {

          //
          // Huge page (1GB).
          //

          const VirtualAddress_t Va(Pml4Idx, PdptIdx);
          AddPage(Va, Pml4e, Pdpte);
          continue;
        }

        const uint64_t PdpteAddress =
            PdptAddress + (PdptIdx * sizeof(uint64_t));
        const uint64_t PdAddress =
            Page::AddressFromPfn(Pdpte.u.PageFrameNumber);
        const auto Pd = (PteHardware_t *)DumpParser_.GetPhysicalPage(PdAddress);
        if (Pd == nullptr) {
          fmt::print("PD:{:#x} not available in the dump, bailing\n",
                     PdAddress);
          continue;
        }

        for (uint64_t PdIdx = 0; PdIdx < NumberEntries; PdIdx++) {
          const auto &Pde = Pd[PdIdx];
          if (!Pde.u.Present) {
            continue;
          }

          if (Pde.u.LargePage) {

            //
            // Large page (2MB).
            //

            const VirtualAddress_t Va(Pml4Idx, PdptIdx, PdIdx);
            AddPage(Va, Pml4e, Pdpte, Pde);
            continue;
          }

          const uint64_t PdeAddress = PdAddress + (PdIdx * sizeof(uint64_t));
          const uint64_t PtAddress =
              Page::AddressFromPfn(Pde.u.PageFrameNumber);
          const auto Pt =
              (PteHardware_t *)DumpParser_.GetPhysicalPage(PtAddress);

          if (Pt == nullptr) {
            fmt::print("PT:{:#x} not available in the dump, skipping\n",
                       PtAddress);
            continue;
          }

          for (uint64_t PtIdx = 0; PtIdx < NumberEntries; PtIdx++) {
            const auto &Pte = Pt[PtIdx];
            if (!Pte.u.Present) {
              continue;
            }

            const uint64_t PteAddress = PtAddress + (PtIdx * sizeof(uint64_t));
            const VirtualAddress_t Va(Pml4Idx, PdptIdx, PdIdx, PtIdx);
            AddPage(Va, Pml4e, Pdpte, Pde, Pte);
          }
        }
      }
    }

    fmt::print("{} pixels have been materialized\n", Colors_.size());
#if 0
    for (auto It = Reverse_.cbegin(); It != Reverse_.cend(); It++) {
      const uint64_t Pa = It->first;
      if (Reverse_.count(Pa) < 2) {
        continue;
      }

      fmt::print("PA {:x}:\n", Pa);
      const auto &Range = Reverse_.equal_range(Pa);
      for (auto It2 = Range.first; It2 != Range.second; It2++) {
        fmt::print("  -> VA {:x}\n", It2->second);
      }

      It = Range.second;
    }
#endif
    return true;
  }
};

} // namespace clairvoyance

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fmt::print("./clairvoyance <filename>\n");
    return 0;
  }

  const uint32_t Order = 9;
  uint32_t Color = 0xff'ff'ff;
  constexpr uint32_t Height = Hilbert::Height(Order);
  constexpr uint32_t Width = Hilbert::Width(Order);
  std::vector<uint32_t> Pixels(Height * Width);

  auto File = fmt::output_file("h.ppm");
  File.print("P3\n{} {}\n255\n", Width, Height);
  for (uint32_t Distance = 0; Distance < Hilbert::NumberPoints(Order);
       Distance++) {
    const auto &[X, Y] = Hilbert::CoordinatesFromDistance(Distance, Order);
    // fmt::print("{} -> ({}, {})\n", Distance, X, Y);
    const uint32_t Idx = (Y * Width) + X;
    Pixels.at(Idx) = Color;
    Color -= 0x10;
  }

  fmt::print("Writing..\n");
  for (const uint32_t Color : Pixels) {
    File.print("{} {} {}\n", (Color >> 0) & 0xff, (Color >> 8) & 0xff,
               (Color >> 16) & 0xff);
  }
  File.close();

  const fs::path DumpFile(argv[1]);
  clairvoyance::DumpColor_t DumpColor;
  if (!DumpColor.ComputePixels(DumpFile)) {
    fmt::print("ComputePixels failed\n");
    return 0;
  }

  fmt::print("Done\n");
  return 1;
}