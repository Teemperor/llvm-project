//===-- MemoryRegionInfo.h ---------------------------------------*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_TARGET_MEMORYREGIONINFO_H
#define LLDB_TARGET_MEMORYREGIONINFO_H

#include <optional>
#include <vector>

#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/RangeMap.h"
#include "lldb/lldb-enumerations.h"
#include "llvm/Support/FormatProviders.h"

namespace lldb_private {
class MemoryRegionInfo {
public:
  typedef Range<lldb::addr_t, lldb::addr_t> RangeType;

  MemoryRegionInfo() = default;
  MemoryRegionInfo(RangeType range, LazyBool read, LazyBool write,
                   LazyBool execute, LazyBool shared, LazyBool mapped,
                   ConstString name)
      : m_range(range), m_read(read), m_write(write), m_execute(execute),
        m_shared(shared), m_mapped(mapped), m_name(name) {}

  RangeType &GetRange() { return m_range; }

  void Clear() { *this = MemoryRegionInfo(); }

  const RangeType &GetRange() const { return m_range; }

  LazyBool GetReadable() const { return m_read; }

  LazyBool GetWritable() const { return m_write; }

  LazyBool GetExecutable() const { return m_execute; }

  LazyBool GetShared() const { return m_shared; }

  LazyBool GetMapped() const { return m_mapped; }

  ConstString GetName() const { return m_name; }

  LazyBool GetMemoryTagged() const { return m_memory_tagged; }

  LazyBool IsShadowStack() const { return m_is_shadow_stack; }

  std::optional<unsigned> GetProtectionKey() const { return m_protection_key; }

  void SetReadable(LazyBool val) { m_read = val; }

  void SetWritable(LazyBool val) { m_write = val; }

  void SetExecutable(LazyBool val) { m_execute = val; }

  void SetShared(LazyBool val) { m_shared = val; }

  void SetMapped(LazyBool val) { m_mapped = val; }

  void SetName(const char *name) { m_name = ConstString(name); }

  LazyBool GetFlash() const { return m_flash; }

  void SetFlash(LazyBool val) { m_flash = val; }

  lldb::offset_t GetBlocksize() const { return m_blocksize; }

  void SetBlocksize(lldb::offset_t blocksize) { m_blocksize = blocksize; }

  MemoryRegionInfo &SetMemoryTagged(LazyBool val) {
    m_memory_tagged = val;
    return *this;
  }

  MemoryRegionInfo &SetIsShadowStack(LazyBool val) {
    m_is_shadow_stack = val;
    return *this;
  }

  MemoryRegionInfo &SetProtectionKey(std::optional<unsigned> key) {
    m_protection_key = key;
    return *this;
  }

  LazyBool GetMaxReadable() const { return m_max_read; }

  LazyBool GetMaxWritable() const { return m_max_write; }

  LazyBool GetMaxExecutable() const { return m_max_execute; }

  void SetMaxReadable(LazyBool val) { m_max_read = val; }

  void SetMaxWritable(LazyBool val) { m_max_write = val; }

  void SetMaxExecutable(LazyBool val) { m_max_execute = val; }

  // Get max protection permissions as a uint32_t bitmask, the same way
  // GetLLDBPermissions() does for the current permissions.
  uint32_t GetMaxLLDBPermissions() const {
    uint32_t permissions = 0;
    if (m_max_read == eLazyBoolYes)
      permissions |= lldb::ePermissionsReadable;
    if (m_max_write == eLazyBoolYes)
      permissions |= lldb::ePermissionsWritable;
    if (m_max_execute == eLazyBoolYes)
      permissions |= lldb::ePermissionsExecutable;
    return permissions;
  }

  std::optional<lldb::ShareMode> GetShareMode() const { return m_share_mode; }

  void SetShareMode(std::optional<lldb::ShareMode> share_mode) {
    m_share_mode = share_mode;
  }

  /// Get the raw region "tag" identifying the kind of allocator that owns
  /// this region (e.g. the platform's VM_MEMORY_* user_tag on macOS).
  std::optional<uint32_t> GetRegionTypeTag() const { return m_region_type; }

  void SetRegionTypeTag(std::optional<uint32_t> region_type) {
    m_region_type = region_type;
  }

  LazyBool IsSubmap() const { return m_is_submap; }

  void SetIsSubmap(LazyBool val) { m_is_submap = val; }

  std::optional<uint32_t> GetNumResidentPages() const {
    return m_pages_resident;
  }

  void SetNumResidentPages(std::optional<uint32_t> count) {
    m_pages_resident = count;
  }

  std::optional<uint32_t> GetNumDirtiedPages() const {
    return m_pages_dirtied;
  }

  void SetNumDirtiedPages(std::optional<uint32_t> count) {
    m_pages_dirtied = count;
  }

  std::optional<uint32_t> GetNumSwappedOutPages() const {
    return m_pages_swapped_out;
  }

  void SetNumSwappedOutPages(std::optional<uint32_t> count) {
    m_pages_swapped_out = count;
  }

  // Get permissions as a uint32_t that is a mask of one or more bits from the
  // lldb::Permissions
  uint32_t GetLLDBPermissions() const {
    uint32_t permissions = 0;
    if (m_read == eLazyBoolYes)
      permissions |= lldb::ePermissionsReadable;
    if (m_write == eLazyBoolYes)
      permissions |= lldb::ePermissionsWritable;
    if (m_execute == eLazyBoolYes)
      permissions |= lldb::ePermissionsExecutable;
    return permissions;
  }

  // Set permissions from a uint32_t that contains one or more bits from the
  // lldb::Permissions
  void SetLLDBPermissions(uint32_t permissions) {
    m_read =
        (permissions & lldb::ePermissionsReadable) ? eLazyBoolYes : eLazyBoolNo;
    m_write =
        (permissions & lldb::ePermissionsWritable) ? eLazyBoolYes : eLazyBoolNo;
    m_execute = (permissions & lldb::ePermissionsExecutable) ? eLazyBoolYes
                                                             : eLazyBoolNo;
  }

  bool operator==(const MemoryRegionInfo &rhs) const {
    return m_range == rhs.m_range && m_read == rhs.m_read &&
           m_write == rhs.m_write && m_execute == rhs.m_execute &&
           m_shared == rhs.m_shared && m_mapped == rhs.m_mapped &&
           m_name == rhs.m_name && m_flash == rhs.m_flash &&
           m_blocksize == rhs.m_blocksize &&
           m_memory_tagged == rhs.m_memory_tagged &&
           m_pagesize == rhs.m_pagesize &&
           m_is_stack_memory == rhs.m_is_stack_memory &&
           m_is_shadow_stack == rhs.m_is_shadow_stack &&
           m_protection_key == rhs.m_protection_key &&
           m_max_read == rhs.m_max_read && m_max_write == rhs.m_max_write &&
           m_max_execute == rhs.m_max_execute &&
           m_share_mode == rhs.m_share_mode &&
           m_region_type == rhs.m_region_type &&
           m_is_submap == rhs.m_is_submap &&
           m_pages_resident == rhs.m_pages_resident &&
           m_pages_dirtied == rhs.m_pages_dirtied &&
           m_pages_swapped_out == rhs.m_pages_swapped_out;
  }

  bool operator!=(const MemoryRegionInfo &rhs) const { return !(*this == rhs); }

  /// Get the target system's VM page size in bytes.
  /// \return
  ///     0 is returned if this information is unavailable.
  int GetPageSize() const { return m_pagesize; }

  /// Get a vector of target VM pages that are dirty -- that have been
  /// modified -- within this memory region.  This is an Optional return
  /// value; it will only be available if the remote stub was able to
  /// detail this.
  const std::optional<std::vector<lldb::addr_t>> &GetDirtyPageList() const {
    return m_dirty_pages;
  }

  LazyBool IsStackMemory() const { return m_is_stack_memory; }

  void SetIsStackMemory(LazyBool val) { m_is_stack_memory = val; }

  void SetPageSize(int pagesize) { m_pagesize = pagesize; }

  void SetDirtyPageList(std::vector<lldb::addr_t> pagelist) {
    if (m_dirty_pages)
      m_dirty_pages->clear();
    m_dirty_pages = std::move(pagelist);
  }

protected:
  RangeType m_range;
  LazyBool m_read = eLazyBoolDontKnow;
  LazyBool m_write = eLazyBoolDontKnow;
  LazyBool m_execute = eLazyBoolDontKnow;
  LazyBool m_shared = eLazyBoolDontKnow;
  LazyBool m_mapped = eLazyBoolDontKnow;
  ConstString m_name;
  LazyBool m_flash = eLazyBoolDontKnow;
  lldb::offset_t m_blocksize = 0;
  LazyBool m_memory_tagged = eLazyBoolDontKnow;
  LazyBool m_is_stack_memory = eLazyBoolDontKnow;
  LazyBool m_is_shadow_stack = eLazyBoolDontKnow;
  std::optional<unsigned> m_protection_key = std::nullopt;
  int m_pagesize = 0;
  std::optional<std::vector<lldb::addr_t>> m_dirty_pages;
  LazyBool m_max_read = eLazyBoolDontKnow;
  LazyBool m_max_write = eLazyBoolDontKnow;
  LazyBool m_max_execute = eLazyBoolDontKnow;
  std::optional<lldb::ShareMode> m_share_mode = std::nullopt;
  std::optional<uint32_t> m_region_type = std::nullopt;
  LazyBool m_is_submap = eLazyBoolDontKnow;
  std::optional<uint32_t> m_pages_resident = std::nullopt;
  std::optional<uint32_t> m_pages_dirtied = std::nullopt;
  std::optional<uint32_t> m_pages_swapped_out = std::nullopt;
};

inline bool operator<(const MemoryRegionInfo &lhs,
                      const MemoryRegionInfo &rhs) {
  return lhs.GetRange() < rhs.GetRange();
}

inline bool operator<(const MemoryRegionInfo &lhs, lldb::addr_t rhs) {
  return lhs.GetRange().GetRangeBase() < rhs;
}

inline bool operator<(lldb::addr_t lhs, const MemoryRegionInfo &rhs) {
  return lhs < rhs.GetRange().GetRangeBase();
}

llvm::raw_ostream &operator<<(llvm::raw_ostream &OS,
                              const MemoryRegionInfo &Info);

// Forward-declarable wrapper.
class MemoryRegionInfos : public std::vector<lldb_private::MemoryRegionInfo> {
public:
  using std::vector<lldb_private::MemoryRegionInfo>::vector;
};

} // namespace lldb_private

namespace llvm {
template <>
/// If Options is empty, prints a textual representation of the value. If
/// Options is a single character, it uses that character for the "yes" value,
/// while "no" is printed as "-", and "don't know" as "?". This can be used to
/// print the permissions in the traditional "rwx" form.
struct format_provider<lldb_private::LazyBool> {
  static void format(const lldb_private::LazyBool &B, raw_ostream &OS,
                     StringRef Options);
};
} // namespace llvm

#endif // LLDB_TARGET_MEMORYREGIONINFO_H
