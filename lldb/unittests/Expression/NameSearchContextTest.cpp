//===-- NameSearchContextTest.cpp -----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ExpressionParser/Clang/NameSearchContext.h"
#include "TestingSupport/SubsystemRAII.h"
#include "TestingSupport/Symbol/ClangTestUtils.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace clang;
using namespace lldb_private;

namespace {
struct NameSearchContextTest : public testing::Test {
  SubsystemRAII<FileSystem, HostInfo> subsystems;
};
} // namespace

TEST_F(NameSearchContextTest, AlreadyFoundVarDecl) {
  TypeSystemClang ts("test AST", HostInfo::GetTargetTriple());
  llvm::SmallVector<NamedDecl *, 4> decls;
  DeclarationName name = clang_utils::getDeclarationName(ts, "name");
  NameSearchContext context(ts, decls, name, ts.GetTranslationUnitDecl());
  // Default state is that no variable has been found.
  EXPECT_FALSE(context.MaybeGetFoundVariable());

  CompilerType record = clang_utils::createRecord(ts, "some_record");
  context.AddNamedDecl(ClangUtil::GetAsTagDecl(record));
  // Didn't find a variable, so AlreadyFoundVarDecl should still be false.
  EXPECT_FALSE(context.MaybeGetFoundVariable());

  QualType int_type(ts.getASTContext().IntTy);
  VarDecl *var = ts.CreateVariableDeclaration(
      ts.GetTranslationUnitDecl(), OptionalClangModuleID(), "name", int_type);
  context.AddNamedDecl(var);
  // Variable was found, so AlreadyFoundVarDecl should now be true.
  EXPECT_TRUE(context.MaybeGetFoundVariable());

  // Add another record and make sure AlreadyFoundVarDecl stays true.
  CompilerType another_record = clang_utils::createRecord(ts, "some_record");
  context.AddNamedDecl(ClangUtil::GetAsTagDecl(another_record));
  EXPECT_TRUE(context.MaybeGetFoundVariable());
}
