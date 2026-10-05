//===-- MemoryRegionInfo.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/MemoryRegionInfo.h"

using namespace lldb_private;

llvm::raw_ostream &lldb_private::operator<<(llvm::raw_ostream &OS,
                                            const MemoryRegionInfo &Info) {
  OS << llvm::formatv(
      "MemoryRegionInfo([{0}, {1}), {2:r}{3:w}{4:x}, "
      "{5}, `{6}`, {7}, {8}, {9}, {10}, {11}, {12})",
      Info.GetRange().GetRangeBase(), Info.GetRange().GetRangeEnd(),
      Info.GetReadable(), Info.GetWritable(), Info.GetExecutable(),
      Info.GetMapped(), Info.GetName(), Info.GetFlash(), Info.GetBlocksize(),
      Info.GetMemoryTagged(), Info.IsStackMemory(), Info.IsShadowStack(),
      Info.GetProtectionKey());
  OS << llvm::formatv(", max={0:r}{1:w}{2:x}, submap={3}",
                      Info.GetMaxReadable(), Info.GetMaxWritable(),
                      Info.GetMaxExecutable(), Info.IsSubmap());
  if (Info.GetShareMode())
    OS << llvm::formatv(", share_mode={0}",
                        static_cast<int>(*Info.GetShareMode()));
  if (Info.GetRegionTypeTag())
    OS << llvm::formatv(", region_type={0}", *Info.GetRegionTypeTag());
  if (Info.GetNumResidentPages())
    OS << llvm::formatv(", pages_resident={0}", *Info.GetNumResidentPages());
  if (Info.GetNumDirtiedPages())
    OS << llvm::formatv(", pages_dirtied={0}", *Info.GetNumDirtiedPages());
  if (Info.GetNumSwappedOutPages())
    OS << llvm::formatv(", pages_swapped_out={0}",
                        *Info.GetNumSwappedOutPages());
  return OS;
}

void llvm::format_provider<LazyBool>::format(const LazyBool &B, raw_ostream &OS,
                                             StringRef Options) {
  assert(Options.size() <= 1);
  bool Empty = Options.empty();
  switch (B) {
  case lldb_private::eLazyBoolNo:
    OS << (Empty ? "no" : "-");
    return;
  case lldb_private::eLazyBoolYes:
    OS << (Empty ? "yes" : Options);
    return;
  case lldb_private::eLazyBoolDontKnow:
    OS << (Empty ? "don't know" : "?");
    return;
  }
}
