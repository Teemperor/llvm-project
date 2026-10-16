//===-- NamedTypeTest.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/NamedType.h"
#include "Plugins/TypeSystem/Clike/Identifier.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

TEST(NamedTypeTest, StoresNames) {
  IdentifierMap ids;
  NamedType<Type> type(
      ids.get("std::vector<int, std::allocator<int> >"),
      DeclScope{/*decl_context=*/nullptr, ids.get("vector<int>")});
  EXPECT_EQ(type.GetName().GetName(), "std::vector<int, std::allocator<int> >");
  EXPECT_EQ(type.GetUnqualifiedName().GetName(), "vector<int>");
  EXPECT_EQ(type.GetDeclContext(), nullptr);
}
