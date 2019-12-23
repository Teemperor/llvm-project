//===-- DWARFASTParserClangTests.cpp ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/SymbolFile/DWARF/DWARFASTParserClang.h"
#include "Plugins/SymbolFile/DWARF/DWARFDIE.h"
#include "TestingSupport/SubsystemRAII.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace lldb;
using namespace lldb_private;

class DWARFASTParserClangTests : public testing::Test {
  SubsystemRAII<FileSystem, ClangASTContext> subsystems;
};

namespace {
class DWARFASTParserClangStub : public DWARFASTParserClang {
public:
  using DWARFASTParserClang::DWARFASTParserClang;
  using DWARFASTParserClang::LinkDeclContextToDIE;

  std::vector<const clang::DeclContext *> GetDeclContextToDIEMapKeys() {
    std::vector<const clang::DeclContext *> keys;
    for (const auto &it : m_decl_ctx_to_die)
      keys.push_back(it.first);
    return keys;
  }
};
} // namespace

TEST_F(DWARFASTParserClangTests,
       EnsureAllDIEsInDeclContextHaveBeenParsedParsesOnlyMatchingEntries) {
  ClangASTContext ast_ctx;
  DWARFASTParserClangStub ast_parser(ast_ctx);

  clang::TranslationUnitDecl *TU = ast_ctx.GetTranslationUnitDecl();
  // Create several unique valid DeclContexts.
  std::vector<clang::DeclContext *> decl_ctxs = {
    ast_ctx.GetUniqueNamespaceDeclaration("a", TU),
    ast_ctx.GetUniqueNamespaceDeclaration("b", TU),
    ast_ctx.GetUniqueNamespaceDeclaration("c", TU),
    ast_ctx.GetUniqueNamespaceDeclaration("d", TU)
  };

  DWARFUnit *unit = nullptr;
  std::vector<DWARFDIE> dies;
  for (clang::DeclContext *d : decl_ctxs)
    dies.emplace_back(unit, (DWARFDebugInfoEntry *)d);

  for (size_t i = 0; i < dies.size(); ++i)
    ast_parser.LinkDeclContextToDIE(decl_ctxs.at(i), dies.at(i));

  ast_parser.EnsureAllDIEsInDeclContextHaveBeenParsed(
      ast_ctx.CreateDeclContext(decl_ctxs[1]));

  EXPECT_THAT(ast_parser.GetDeclContextToDIEMapKeys(),
              testing::UnorderedElementsAre(decl_ctxs[0], decl_ctxs[3]));
}
