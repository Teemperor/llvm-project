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
    m_std_inc = dir.str();
  if (dir.endswith("/usr/include/bits")) {
    llvm::SmallVector<char, 256> path;
    llvm::sys::path::remove_dots();
    m_usr_inc = llvm::sys::path::remove_dots(dir.str() + "/..", true).str();
  }
  if (dir.endswith("/usr/include"))
    m_usr_inc = dir.str();
}

bool CppModuleConfiguration::hasValidConfig() {
  return !m_usr_inc.empty() && !m_std_inc.empty() && !m_resource_inc.empty();
}

CppModuleConfiguration::CppModuleConfiguration(const FileSpecList &support_files) {
  for (const FileSpec &f : support_files)
    handleFile(f);

  m_resource_inc = GetClangResourceDir().GetPath() + "/include";

  if (hasValidConfig()){
    m_include_dirs = {m_std_inc, m_resource_inc, m_usr_inc};
    m_imported_modules = {"std"};
  }
}
