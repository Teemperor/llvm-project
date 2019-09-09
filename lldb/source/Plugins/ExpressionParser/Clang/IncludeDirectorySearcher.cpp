//===-- ASTDumper.cpp -------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "IncludeDirectorySearcher.h"

#include "ClangHost.h"
#include "lldb/Host/FileSystem.h"

using namespace lldb_private;


IncludeDirectorySearcher::IncludeDirectorySearcher(const FileSpecList &support_files) {
  for (const FileSpec &f : support_files) {
    handleFile(f);
  }

  m_resource_inc = ConstString(GetClangResourceDir().GetPath() + "/include");

  m_include_dirs = {m_std_inc, m_resource_inc, m_usr_inc};
  m_imported_modules = {"std"};

  llvm::errs() << "------------FINAL DIRS:------------\n";
  for (auto&s : m_include_dirs) {
    llvm::errs() << s.GetStringRef() << "\n";
  }
  llvm::errs() << "------------------------\n";
}
