//===-- SourceModule.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_SourceModule_h_
#define liblldb_SourceModule_h_

#include "lldb/Utility/ConstString.h"
#include <vector>

namespace lldb_private {

/// Information needed to import a source-language module.
struct SourceModule {
  /// Something like "Module.Submodule".
  std::vector<ConstString> path;
  ConstString search_path;
  ConstString sysroot;

  /// Comparison operator for two SourceModules. Only useful for imposing a
  /// total order on list of SourceModules so that they can be std::sorted.
  bool operator<(const SourceModule &o) const {
    return std::tie(path, search_path, sysroot) <
           std::tie(o.path, o.search_path, o.sysroot);
  }

  bool operator==(const SourceModule &o) const {
    return std::tie(path, search_path, sysroot) ==
           std::tie(o.path, o.search_path, o.sysroot);
  }
};

} // namespace lldb_private

#endif
