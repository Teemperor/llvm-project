//===-- ClangASTGeneratorTestUtils.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_UNITTESTS_TYPESYSTEM_CLIKE_CLANGASTGENERATORTESTUTILS_H
#define LLDB_UNITTESTS_TYPESYSTEM_CLIKE_CLANGASTGENERATORTESTUTILS_H

#include "Plugins/ExpressionParser/Clang/ClangASTGenerator.h"
#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "TestingSupport/SubsystemRAII.h"
#include "lldb/Host/FileSystem.h"

#include "clang/AST/ASTContext.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"

#include "gtest/gtest.h"

namespace lldb_private {

/// Owns a standalone clang::ASTContext with builtin types initialized for
/// x86_64-pc-linux-gnu, mirroring what ClangASTGenerator::DumpRecords sets up
/// for `target modules dump ast`. Used by tests that exercise
/// ClangASTGenerator/ClangTypeConverter without going through a full
/// expression-evaluation pipeline.
class ClangASTGeneratorTestUtils : public testing::Test {
public:
  SubsystemRAII<FileSystem> subsystems;

  clang::LangOptions lang_opts;
  clang::IdentifierTable idents{lang_opts, nullptr};
  clang::Builtin::Context builtins;
  clang::SelectorTable selectors;
  clang::FileSystemOptions file_system_options;
  clang::FileManager file_manager{file_system_options,
                                  FileSystem::Instance().GetVirtualFileSystem()};
  std::shared_ptr<clang::DiagnosticOptions> diag_options =
      std::make_shared<clang::DiagnosticOptions>();
  clang::DiagnosticsEngine diagnostics{clang::DiagnosticIDs::create(),
                                       *diag_options};
  clang::SourceManager source_manager{diagnostics, file_manager};
  clang::ASTContext ast{lang_opts,
                        source_manager,
                        idents,
                        selectors,
                        builtins,
                        clang::TranslationUnitKind::TU_Complete};

  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::Triple("x86_64-pc-linux-gnu"));
  clike_typesystem::Builder builder{*ts};

  void SetUp() override {
    lang_opts.CPlusPlus = true;
    lang_opts.CPlusPlus11 = true;
    auto target_options = std::make_shared<clang::TargetOptions>();
    target_options->Triple = "x86_64-pc-linux-gnu";
    if (clang::TargetInfo *target_info = clang::TargetInfo::CreateTargetInfo(
            ast.getDiagnostics(), *target_options))
      ast.InitBuiltinTypes(*target_info);
  }

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }
};

} // namespace lldb_private

#endif
