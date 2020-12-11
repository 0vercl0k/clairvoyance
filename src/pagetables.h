// Axel '0vercl0k' Souchet - December 11 2020
#pragma once
#include "kdmp-parser.h"
#include <cstdint>
#include <optional>

namespace page {

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

} // namespace page

namespace ptables {

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

enum class PageType_t { Huge, Large, Normal };

//
// Get an absolute address from a PFN (with a base).
//

static constexpr uint64_t AddressFromPfn(const uint64_t Pfn) {
  return Pfn * page::Size;
}
static constexpr uint64_t AddressFromPfn(const uint64_t Base,
                                         const uint64_t Pfn) {
  return Base + (Pfn * page::Size);
}

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

  static constexpr uint64_t NumberEntries = (page::Size / sizeof(uint64_t));

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
    Entry.PdpteAddress =
        AddressFromPfn(Pml4e_->u.PageFrameNumber) + uint64_t(Pdpte_ - Pdpt_);

    if (Pdpte_->u.LargePage) {
      Entry.Pde = Entry.PdeAddress = 0;
      Entry.Pte = Entry.PteAddress = 0;
      Entry.Pa = AddressFromPfn(Pdpte_->u.PageFrameNumber);
      Entry.Va = VirtualAddress_t(IndexFromPxe(Pml4_, Pml4e_),
                                  IndexFromPxe(Pdpt_, Pdpte_))
                     .U64();
      Entry.Type = PageType_t::Huge;
      return Entry;
    }

    Entry.Pde = *Pde_;
    Entry.PdeAddress =
        AddressFromPfn(Pdpte_->u.PageFrameNumber) + uint64_t(Pde_ - Pd_);

    if (Pde_->u.LargePage) {
      Entry.Pte = Entry.PteAddress = 0;
      Entry.Pa = AddressFromPfn(Pde_->u.PageFrameNumber);
      Entry.Va =
          VirtualAddress_t(IndexFromPxe(Pml4_, Pml4e_),
                           IndexFromPxe(Pdpt_, Pdpte_), IndexFromPxe(Pd_, Pde_))
              .U64();
      Entry.Type = PageType_t::Large;
      return Entry;
    }

    Entry.Pte = *Pte_;
    Entry.PteAddress =
        AddressFromPfn(Pde_->u.PageFrameNumber) + uint64_t(Pte_ - Pt_);

    Entry.Pa = AddressFromPfn(Pte_->u.PageFrameNumber);
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

      const uint64_t PdptAddress = AddressFromPfn(Pml4e_->u.PageFrameNumber);
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

        const uint64_t PdAddress = AddressFromPfn(Pdpte_->u.PageFrameNumber);
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

          const uint64_t PtAddress = AddressFromPfn(Pde_->u.PageFrameNumber);
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
}
} // namespace ptables