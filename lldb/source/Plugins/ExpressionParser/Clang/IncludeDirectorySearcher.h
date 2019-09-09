//===-- IncludeDirectorySearcher.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <lldb/Core/FileSpecList.h>

#ifndef liblldb_IncludeDirectorySearcher_h_
#define liblldb_IncludeDirectorySearcher_h_

namespace lldb_private {

class IncludeDirectorySearcher {
  void handleFile(const FileSpec &f) {
    if (f.GetDirectory().GetStringRef().endswith("/include/c++/v1"))
      m_std_inc = f.GetDirectory();
    if (f.GetDirectory().GetStringRef().endswith("/usr/include/bits"))
      m_usr_inc = ConstString(f.GetDirectory().GetStringRef().str() + "/..");
    if (f.GetDirectory().GetStringRef().endswith("/usr/include"))
      m_usr_inc = ConstString(f.GetDirectory().GetStringRef().str());
  }

  ConstString m_resource_inc;
  ConstString m_std_inc;
  ConstString m_usr_inc;

  std::vector<ConstString> m_include_dirs;
  std::vector<std::string> m_imported_modules;
public:
  IncludeDirectorySearcher(const FileSpecList &support_files);

  llvm::ArrayRef<ConstString> GetIncludeDirs() const {
    return m_include_dirs;
  }

  llvm::ArrayRef<std::string> GetImportedModules() const {
    return m_imported_modules;
  }
};

} // namespace lldb_private

#endif
