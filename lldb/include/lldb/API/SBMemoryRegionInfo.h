//===-- SBMemoryRegionInfo.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_API_SBMEMORYREGIONINFO_H
#define LLDB_API_SBMEMORYREGIONINFO_H

#include "lldb/API/SBData.h"
#include "lldb/API/SBDefines.h"

namespace lldb {

class LLDB_API SBMemoryRegionInfo {
public:
  SBMemoryRegionInfo();

  SBMemoryRegionInfo(const lldb::SBMemoryRegionInfo &rhs);

  SBMemoryRegionInfo(const char *name, lldb::addr_t begin, lldb::addr_t end,
                     uint32_t permissions, bool mapped,
                     bool stack_memory = false);

  ~SBMemoryRegionInfo();

  const lldb::SBMemoryRegionInfo &
  operator=(const lldb::SBMemoryRegionInfo &rhs);

  void Clear();

  /// Get the base address of this memory range.
  ///
  /// \return
  ///     The base address of this memory range.
  lldb::addr_t GetRegionBase();

  /// Get the end address of this memory range.
  ///
  /// \return
  ///     The base address of this memory range.
  lldb::addr_t GetRegionEnd();

  /// Check if this memory address is marked readable to the process.
  ///
  /// \return
  ///     true if this memory address is marked readable
  bool IsReadable();

  /// Check if this memory address is marked writable to the process.
  ///
  /// \return
  ///     true if this memory address is marked writable
  bool IsWritable();

  /// Check if this memory address is marked executable to the process.
  ///
  /// \return
  ///     true if this memory address is marked executable
  bool IsExecutable();

  /// Check if this memory address is mapped into the process address
  /// space.
  ///
  /// \return
  ///     true if this memory address is in the process address space.
  bool IsMapped();

  /// Returns the name of the memory region mapped at the given
  /// address.
  ///
  /// \return
  ///     In case of memory mapped files it is the absolute path of
  ///     the file otherwise it is a name associated with the memory
  ///     region. If no name can be determined the returns nullptr.
  const char *GetName();

  /// Returns whether this memory region has a list of memory pages
  /// that have been modified -- that are dirty.
  ///
  /// \return
  ///     True if the dirty page list is available.
  bool HasDirtyMemoryPageList();

  /// Returns the number of modified pages -- dirty pages -- in this
  /// memory region.
  ///
  /// \return
  ///     The number of dirty page entries will be returned.  If
  ///     there are no dirty pages in this memory region, 0 will
  ///     be returned.  0 will also be returned if the dirty page
  ///     list is not available for this memory region -- you must
  ///     use HasDirtyMemoryPageList() to check for that.
  uint32_t GetNumDirtyPages();

  /// Returns the address of a memory page that has been modified in
  /// this region.
  ///
  /// \return
  ///     Returns the address for his dirty page in the list.
  ///     If this memory region does not have a dirty page list,
  ///     LLDB_INVALID_ADDRESS is returned.
  addr_t GetDirtyPageAddressAtIndex(uint32_t idx);

  /// Returns the size of a memory page in this region.
  ///
  /// \return
  ///     Returns the size of the memory pages in this region,
  ///     or 0 if this information is unavailable.
  int GetPageSize();

  /// Check if this memory address is marked readable through the
  /// maximum protection this region can be set to (e.g. via mprotect).
  ///
  /// \return
  ///     true if this memory address's maximum protection is readable
  bool IsMaxReadable();

  /// Check if this memory address is marked writable through the
  /// maximum protection this region can be set to (e.g. via mprotect).
  ///
  /// \return
  ///     true if this memory address's maximum protection is writable
  bool IsMaxWritable();

  /// Check if this memory address is marked executable through the
  /// maximum protection this region can be set to (e.g. via mprotect).
  ///
  /// \return
  ///     true if this memory address's maximum protection is executable
  bool IsMaxExecutable();

  /// Returns whether this memory region's share mode is known.
  ///
  /// \return
  ///     True if the share mode is available.
  bool HasShareMode();

  /// Returns the share mode of this memory region.
  ///
  /// \return
  ///     The lldb::ShareMode for this region. Only valid if
  ///     HasShareMode() returns true.
  lldb::ShareMode GetShareMode();

  /// Returns whether this memory region has a known region type tag.
  ///
  /// \return
  ///     True if the region type tag is available.
  bool HasRegionTypeTag();

  /// Returns the raw region type tag for this memory region -- a
  /// platform-specific value identifying the kind of allocator that
  /// owns this region (e.g. the VM_MEMORY_* user tag on macOS).
  ///
  /// \return
  ///     The raw region type tag. Only valid if HasRegionTypeTag()
  ///     returns true.
  uint32_t GetRegionTypeTag();

  /// Check whether this memory region is a submap.
  ///
  /// \return
  ///     true if this memory region is a submap.
  bool IsSubmap();

  /// Returns whether this memory region has a known count of resident
  /// pages.
  ///
  /// \return
  ///     True if the resident page count is available.
  bool HasNumResidentPages();

  /// Returns the number of resident pages in this memory region.
  ///
  /// \return
  ///     The number of resident pages. Only valid if
  ///     HasNumResidentPages() returns true.
  uint32_t GetNumResidentPages();

  /// Returns whether this memory region has a known count of dirtied
  /// pages.
  ///
  /// \return
  ///     True if the dirtied page count is available.
  bool HasNumDirtiedPages();

  /// Returns the number of dirtied (modified) pages in this memory
  /// region.
  ///
  /// \return
  ///     The number of dirtied pages. Only valid if
  ///     HasNumDirtiedPages() returns true.
  uint32_t GetNumDirtiedPages();

  /// Returns whether this memory region has a known count of pages
  /// swapped out.
  ///
  /// \return
  ///     True if the swapped-out page count is available.
  bool HasNumSwappedOutPages();

  /// Returns the number of pages swapped out for this memory region.
  ///
  /// \return
  ///     The number of swapped-out pages. Only valid if
  ///     HasNumSwappedOutPages() returns true.
  uint32_t GetNumSwappedOutPages();

  bool operator==(const lldb::SBMemoryRegionInfo &rhs) const;

  bool operator!=(const lldb::SBMemoryRegionInfo &rhs) const;

  /// writes a description of the memory region to a SBStream.
  ///
  /// \param[in,out] description
  ///     A stream object where the description will be written.
  ///
  /// \return
  ///     Returns true if the description was successfully written,
  ///     false otherwise.
  ///
  /// The description format is: [Hex start - Hex End) with associated
  /// permissions (RWX)
  bool GetDescription(lldb::SBStream &description);

private:
  friend class SBProcess;
  friend class SBMemoryRegionInfoList;
  friend class SBSaveCoreOptions;
  friend class lldb_private::ScriptInterpreter;

  lldb_private::MemoryRegionInfo &ref();

  const lldb_private::MemoryRegionInfo &ref() const;

  // Unused.
  SBMemoryRegionInfo(const lldb_private::MemoryRegionInfo *lldb_object_ptr);

  lldb::MemoryRegionInfoUP m_opaque_up;
};

} // namespace lldb

#endif // LLDB_API_SBMEMORYREGIONINFO_H
