// Axel '0vercl0k' Souchet - February 15 2019
#pragma once

#include "filemap.h"
#include "kdmp-parser-structs.h"
#include <cstdint>
#include <unordered_map>

namespace kdmpparser {

using Physmem_t = std::unordered_map<uint64_t, const uint8_t *>;

struct BugCheckParameters_t {
  uint32_t BugCheckCode;
  uint64_t BugCheckCodeParameter[4];
};

class KernelDumpParser {
private:
  //
  // The mapped file.
  //

  FileMap_t FileMap_;

  //
  // Header of the crash-dump.
  //

  HEADER64 *DmpHdr_ = nullptr;

  //
  // File path to the crash-dump.
  //

  const char *PathFile_ = nullptr;

  //
  // Mapping between physical addresses / page data.
  //

  Physmem_t Physmem_;

public:
  //
  // Actually do the parsing of the file.
  //

  bool Parse(const char *PathFile);

  //
  // Give the Context record to the user.
  //

  const CONTEXT *GetContext();

  //
  // Give the bugcheck parameters to the user.
  //

  const BugCheckParameters_t GetBugCheckParameters();

  //
  // Get the type of dump.
  //

  DumpType_t GetDumpType();

  //
  // Get the physmem.
  //

  const Physmem_t &GetPhysmem();

  //
  // Show the exception record.
  //

  void ShowExceptionRecord(const uint32_t Prefix) const;

  //
  // Show the context record.
  //

  void ShowContextRecord(const uint32_t Prefix) const;

  //
  // Show all the structures of the dump.
  //

  void ShowAllStructures(const uint32_t Prefix) const;

  //
  // Get the content of a physical address.
  //

  const uint8_t *GetPhysicalPage(const uint64_t PhysicalAddress) const;

  //
  // Get the directory table base.
  //

  uint64_t GetDirectoryTableBase() const;

  //
  // Translate a virtual address to physical address using a directory table
  // base.
  //

  uint64_t VirtTranslate(const uint64_t VirtualAddress,
                         const uint64_t DirectoryTableBase = 0) const;

  //
  // Get the content of a virtual address.
  //

  const uint8_t *GetVirtualPage(const uint64_t VirtualAddress,
                                const uint64_t DirectoryTableBase = 0) const;

private:
  //
  // Utility function to read an uint64_t from a physical address.
  //

  uint64_t PhyRead8(const uint64_t PhysicalAddress) const;

  //
  // Build a map of physical addresses / page data pointers for full dump.
  //

  bool BuildPhysmemFullDump();

  //
  // Build a map of physical addresses / page data pointers for BMP dump.
  //

  bool BuildPhysmemBMPDump();

  //
  // Parse the DMP_HEADER.
  //

  bool ParseDmpHeader();

  //
  // Map a view of the file in memory.
  //

  bool MapFile();
};
} // namespace kdmpparser
