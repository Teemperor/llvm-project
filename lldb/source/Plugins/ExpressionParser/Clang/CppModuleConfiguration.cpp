//===-- ASTDumper.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CppModuleConfiguration.h"

#include "ClangHost.h"
#include "lldb/Host/FileSystem.h"

using namespace lldb_private;

void CppModuleConfiguration::handleFile(const FileSpec &f) {
  llvm::StringRef dir = f.GetDirectory().GetStringRef();

  if (dir.endswith("/include/c++/v1"))
    m_std_inc = f.GetDirectory();
  if (dir.endswith("/usr/include/bits"))
    m_usr_inc = ConstString(dir.str() + "/..");
  if (dir.endswith("/usr/include"))
    m_usr_inc = f.GetDirectory();
}

bool CppModuleConfiguration::hasValidConfig() {
  return !m_usr_inc.IsEmpty() && !m_std_inc.IsEmpty() && !m_resource_inc.IsEmpty();
}

CppModuleConfiguration::CppModuleConfiguration(const FileSpecList &support_files) {
  for (const FileSpec &f : support_files)
    handleFile(f);

  m_resource_inc = ConstString(GetClangResourceDir().GetPath() + "/include");

  if (hasValidConfig()){
    m_include_dirs = {m_std_inc, m_resource_inc, m_usr_inc};
    m_imported_modules = {"std"};
  }
}
