// Axel '0vercl0k' Souchet - December 1 2020
#include "fmt/format.h"
#include "fmt/os.h"
#include "hilbert.h"
#include "kdmp-parser.h"
#include <cmath>
#include <filesystem>
#include <optional>
#include <unordered_map>

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

enum class Properties_t : uint8_t {
  None,
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
  case None:
    return "None";
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

const std::unordered_map<Properties_t, uint32_t> PropertiesToRgb = {
    {Properties_t::None, color::Black},
    {Properties_t::UserRead, color::PaleGreen},
    {Properties_t::UserReadExec, color::CanaryYellow},
    {Properties_t::UserReadWrite, color::Mauve},
    {Properties_t::UserReadWriteExec, color::LightRed},
    {Properties_t::KernelRead, color::Green},
    {Properties_t::KernelReadExec, color::Yellow},
    {Properties_t::KernelReadWrite, color::Purple},
    {Properties_t::KernelReadWriteExec, color::Red}};

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
  uint64_t AsUINT64 = 0;

  constexpr PteHardware_t() = default;
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

enum class PageType_t { Huge, Large, Normal };

static constexpr uint64_t GetNumberPixels(const PageType_t PageType) {
  using enum PageType_t;
  switch (PageType) {
  case Huge: {

    //
    // Huge pages are 1GB so we'll add the below number of pixels.
    //

    return (1024 * 1024 * 1024) / Page::Size;
  }

  case Large: {

    //
    // Large pages are 2MB so we'll add the below number of pixels.
    //

    return (1024 * 1024 * 2) / Page::Size;
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

#if 0
class DumpColor_t {
private:
  kdmpparser::KernelDumpParser DumpParser_;
  std::vector<Properties_t> Tape_;
  std::unordered_multimap<uint64_t, uint64_t> Reverse_;
  uint64_t LastVa_ = 0;

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

    if ((LastVa_ + Page::Size) != Va.U64()) {
      //
      // We have a gap, split it in 2mb chunk of black pixels.
      //

      uint64_t N = 0;
      constexpr uint64_t _2Mb = 1024 * 1024 * 2;
      constexpr uint64_t MaxGapEntries = 10'000;
      for (uint64_t LastVa = LastVa_; (LastVa + _2Mb) < Va.U64();
           LastVa += _2Mb) {
        Tape_.emplace_back(Properties_t::None);
        N++;
        if (N >= MaxGapEntries) {
          fmt::print("Huge gap from {:x} to {:x}, skipping\n", LastVa_,
                     Va.U64());
          break;
        }
      }

      // if (N > 0) {
      //  fmt::print("Filled a gap with {} {:x} {:x}\n", N, LastVa_, Va.U64());
      //}
    }

    const uint64_t NumberPixels = GetNumberPixels(PageType);
    const auto Properties = PropertiesFromPtes(Pml4e, Pdpte, Pde, Pte);
    for (uint64_t Idx = 0; Idx < NumberPixels; Idx++) {
      const uint64_t CurrentPa = Page::AddressFromPfn(Pa, Idx);
      const uint64_t CurrentVa = Page::AddressFromPfn(Va.U64(), Idx);
      LastVa_ = CurrentVa;

      if (Reverse_.count(CurrentPa) >= 10) {
        continue;
      }

      // fmt::print("VA:{:#x} ({}, {}) PA:{:#x}\n", CurrentVa,
      //           PropertiesToString(Properties),
      //           PageType != PageType_t::NormalPage ? ">4k" : "4k",
      //           CurrentPa);
      Tape_.emplace_back(Properties);
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
      fmt::print("/!\\ {} is not a full dump so some pages might be missing\n",
                 DumpFile.filename().string());
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
#endif

class PageTableIterator_t {
  const kdmpparser::KernelDumpParser &DumpParser_;
  uint64_t DirectoryAddress_ = 0;
  const PteHardware_t *Pml4_ = nullptr;
  const PteHardware_t *Pml4e_ = nullptr;

  const PteHardware_t *Pdpt_ = nullptr;
  const PteHardware_t *Pdpte_ = nullptr;

  const PteHardware_t *Pd_ = nullptr;
  const PteHardware_t *Pde_ = nullptr;

  const PteHardware_t *Pt_ = nullptr;
  const PteHardware_t *Pte_ = nullptr;

  struct Entry_t {
    PteHardware_t Pml4e;
    uint64_t Pml4eAddress;

    PteHardware_t Pdpte;
    uint64_t PdpteAddress;

    PteHardware_t Pde;
    uint64_t PdeAddress;

    PteHardware_t Pte;
    uint64_t PteAddress;

    uint64_t Pa;
    uint64_t Va;
    PageType_t Type;
  };

  static constexpr uint64_t NumberEntries = (Page::Size / sizeof(uint64_t));

  static constexpr uint64_t IndexFromPxe(const PteHardware_t *Directory,
                                         const PteHardware_t *Entry) {
    return uint64_t(Entry - Directory);
  }

  static constexpr const PteHardware_t *Limit(const PteHardware_t *Directory) {
    return Directory + NumberEntries;
  }

  static constexpr std::optional<const PteHardware_t *>
  FindNextPresentEntry(const PteHardware_t *Directory,
                       const PteHardware_t *Entry_ = nullptr) {
    bool Found = false;
    const PteHardware_t *Entry = Entry_ == nullptr ? Directory : Entry_;
    while (Entry < Limit(Directory)) {
      if (Entry->u.Present) {
        Found = true;
        break;
      }
      Entry++;
    }

    if (!Found) {
      return std::nullopt;
    }

    return Entry;
  }

  Entry_t MakeEntry() const {
    Entry_t Entry;
    Entry.Pml4e = *Pml4e_;
    Entry.Pml4eAddress = DirectoryAddress_ + uint64_t(Pml4e_ - Pml4_);

    Entry.Pdpte = *Pdpte_;
    Entry.PdpteAddress = Page::AddressFromPfn(Pml4e_->u.PageFrameNumber) +
                         uint64_t(Pdpte_ - Pdpt_);

    if (Pdpte_->u.LargePage) {
      Entry.Pde = Entry.PdeAddress = 0;
      Entry.Pte = Entry.PteAddress = 0;
      Entry.Pa = Page::AddressFromPfn(Pdpte_->u.PageFrameNumber);
      Entry.Va = VirtualAddress_t(IndexFromPxe(Pml4_, Pml4e_),
                                  IndexFromPxe(Pdpt_, Pdpte_))
                     .U64();
      Entry.Type = PageType_t::Huge;
      return Entry;
    }

    Entry.Pde = *Pde_;
    Entry.PdeAddress =
        Page::AddressFromPfn(Pdpte_->u.PageFrameNumber) + uint64_t(Pde_ - Pd_);

    if (Pde_->u.LargePage) {
      Entry.Pte = Entry.PteAddress = 0;
      Entry.Pa = Page::AddressFromPfn(Pde_->u.PageFrameNumber);
      Entry.Va =
          VirtualAddress_t(IndexFromPxe(Pml4_, Pml4e_),
                           IndexFromPxe(Pdpt_, Pdpte_), IndexFromPxe(Pd_, Pde_))
              .U64();
      Entry.Type = PageType_t::Large;
      return Entry;
    }

    Entry.Pte = *Pte_;
    Entry.PteAddress =
        Page::AddressFromPfn(Pde_->u.PageFrameNumber) + uint64_t(Pte_ - Pt_);

    Entry.Pa = Page::AddressFromPfn(Pte_->u.PageFrameNumber);
    Entry.Va = VirtualAddress_t(
                   IndexFromPxe(Pml4_, Pml4e_), IndexFromPxe(Pdpt_, Pdpte_),
                   IndexFromPxe(Pd_, Pde_), IndexFromPxe(Pt_, Pte_))
                   .U64();
    Entry.Type = PageType_t::Normal;
    return Entry;
  }

  void Reset() {
    Pml4_ = (PteHardware_t *)DumpParser_.GetPhysicalPage(DirectoryAddress_);
    Pml4e_ = Pml4_;
  }

public:
  explicit PageTableIterator_t(const kdmpparser::KernelDumpParser &DumpParser,
                               const uint64_t DirectoryAddress)
      : DumpParser_(DumpParser), DirectoryAddress_(DirectoryAddress) {
    Reset();
  }

  std::optional<Entry_t> Next() {

    //
    // First level.
    //

    for (; Pml4e_ < Limit(Pml4_); Pml4e_++) {
      if (!Pml4e_->u.Present) {
        continue;
      }

      const uint64_t PdptAddress =
          Page::AddressFromPfn(Pml4e_->u.PageFrameNumber);

      const auto OldPdpt = Pdpt_;
      Pdpt_ = (PteHardware_t *)DumpParser_.GetPhysicalPage(PdptAddress);

      if (Pdpt_ == nullptr) {
        fmt::print("PDPT:{:#x} not available in the dump, bailing\n",
                   PdptAddress);
        continue;
      }

      if (OldPdpt != Pdpt_) {
        Pdpte_ = Pdpt_;
      }

      //
      // Second level.
      //

      for (; Pdpte_ < Limit(Pdpt_); Pdpte_++) {
        if (!Pdpte_->u.Present) {
          continue;
        }

        if (Pdpte_->u.LargePage) {

          //
          // Huge page (1GB).
          //

          Pd_ = Pde_ = Pt_ = Pte_ = nullptr;
          const auto &Entry = MakeEntry();
          Pdpte_++;
          return Entry;
        }

        const uint64_t PdAddress =
            Page::AddressFromPfn(Pdpte_->u.PageFrameNumber);

        const auto OldPd = Pd_;
        Pd_ = (PteHardware_t *)DumpParser_.GetPhysicalPage(PdAddress);

        if (Pd_ == nullptr) {
          fmt::print("PD:{:#x} not available in the dump, bailing\n",
                     PdAddress);
          continue;
        }

        if (OldPd != Pd_) {
          Pde_ = Pd_;
        }

        //
        // Third level.
        //

        for (; Pde_ < Limit(Pd_); Pde_++) {
          if (!Pde_->u.Present) {
            continue;
          }

          if (Pde_->u.LargePage) {

            //
            // Large page (2MB).
            //

            Pt_ = Pte_ = nullptr;
            const auto &Entry = MakeEntry();
            Pde_++;
            return Entry;
          }

          const uint64_t PtAddress =
              Page::AddressFromPfn(Pde_->u.PageFrameNumber);

          const auto OldPt = Pt_;
          Pt_ = (PteHardware_t *)DumpParser_.GetPhysicalPage(PtAddress);

          if (Pt_ == nullptr) {
            fmt::print("PT:{:#x} not available in the dump, skipping\n",
                       PtAddress);
            continue;
          }

          if (OldPt != Pt_) {
            Pte_ = Pt_;
          }

          //
          // Fourth level.
          //

          for (; Pte_ < Limit(Pt_); Pte_++) {
            if (!Pte_->u.Present) {
              continue;
            }

            const auto &Entry = MakeEntry();
            Pte_++;
            return Entry;
          }
        }
      }
    }

    return std::nullopt;
  }
};
} // namespace clairvoyance

int main(int argc, char *argv[]) {
  if (argc != 2) {
    fmt::print("./clairvoyance <filename>\n");
    return 0;
  }

  const fs::path DumpFile(argv[1]);
  kdmpparser::KernelDumpParser Parser;
  if (!Parser.Parse(DumpFile.string().c_str())) {
    fmt::print("Parse failed\n");
    return false;
  }

  clairvoyance::PageTableIterator_t It(Parser, Parser.GetDirectoryTableBase());
  std::vector<clairvoyance::Properties_t> Tape;
  Tape.reserve(500'000);

  uint64_t LastVa = 0;
  while (1) {
    const auto &Entry = It.Next();
    if (!Entry) {
      break;
    }

    if ((LastVa + clairvoyance::Page::Size) != Entry->Va) {

      //
      // We have a gap, split it in 2mb chunk of black pixels.
      //

      uint64_t N = 0;
      constexpr uint64_t MaxGapEntries = 10'000;
      for (uint64_t CurLastVa = LastVa;
           (CurLastVa + clairvoyance::Page::Size) < Entry->Va;
           CurLastVa += clairvoyance::Page::Size) {
        Tape.emplace_back(clairvoyance::Properties_t::None);
        N++;
        if (N >= MaxGapEntries) {
          fmt::print("Huge gap from {:x} to {:x}, skipping\n", LastVa,
                     Entry->Va);
          break;
        }
      }
    }

    const auto &Properties = clairvoyance::PropertiesFromPtes(
        Entry->Pml4e, Entry->Pdpte, Entry->Pde, Entry->Pte);
    const uint64_t NumberPixels = clairvoyance::GetNumberPixels(Entry->Type);
    for (uint64_t Idx = 0; Idx < NumberPixels; Idx++) {
      const uint64_t CurrentPa =
          clairvoyance::Page::AddressFromPfn(Entry->Pa, Idx);
      const uint64_t CurrentVa =
          clairvoyance::Page::AddressFromPfn(Entry->Va, Idx);
      LastVa = CurrentVa;

      // fmt::print("VA:{:#x} ({}, {}) PA:{:#x}\n", CurrentVa,
      //           PropertiesToString(Properties),
      //           Entry->Type != clairvoyance::PageType_t::Normal ? ">4k" :
      //           "4k", CurrentPa);
      Tape.emplace_back(Properties);
    }
  }

  const uint64_t Log2 = std::log2(float(Tape.size()));
  const uint64_t Order = (Log2 / 2);
  const uint64_t Height = Hilbert::Height(Order);
  const uint64_t Width = Hilbert::Width(Order);

  fmt::print("{} props have been materialized; will get layed out on hcurve "
             "order {} ({} pixels)\n",
             Tape.size(), Order, Width * Height);

  auto File = fmt::output_file("vis.ppm");
  File.print("P3\n{} {}\n255\n", Width, Height);

  for (uint64_t Y = 0; Y < Height; Y++) {
    for (uint64_t X = 0; X < Width; X++) {
      const auto &Distance = Hilbert::DistanceFromCoordinates(X, Y, Order);
      // const auto &[OX, OY] = Hilbert::CoordinatesFromDistance(Distance,
      // Order); if (OX != X || OY != Y) {
      //  fmt::print("^___ mismatch ({}/{} VS {}/{})\n", X, Y, OX, OY);
      //  __debugbreak();
      //}

      const uint32_t Rgb =
          (Distance < Tape.size())
              ? clairvoyance::PropertiesToRgb.at(Tape[Distance])
              : clairvoyance::color::White;
      if (Rgb == 0xffffff) {
        __debugbreak();
      }
      File.print("{} {} {}\n", (Rgb >> 16) & 0xff, (Rgb >> 8) & 0xff,
                 (Rgb >> 0) & 0xff);
    }
    File.print("\n");
  }

  fmt::print("Done\n");
  return 1;
}