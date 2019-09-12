//===-- IncludeDirectorySearcher.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include <lldb/Core/FileSpecList.h>

#ifndef liblldb_CppModuleConfiguration_h_
#define liblldb_CppModuleConfiguration_h_

namespace lldb_private {

class CppModuleConfiguration {
  std::string m_resource_inc;
  std::string m_std_inc;
  std::string m_usr_inc;

  std::vector<std::string> m_include_dirs;
  std::vector<std::string> m_imported_modules;

  void handleFile(const FileSpec &f);

  bool hasValidConfig();
public:
  explicit CppModuleConfiguration(const FileSpecList &support_files);
  CppModuleConfiguration() {}

  llvm::ArrayRef<std::string> GetIncludeDirs() const {
    return m_include_dirs;
  }

  llvm::ArrayRef<std::string> GetImportedModules() const {
    return m_imported_modules;
  }
};

} // namespace lldb_private

#endif
