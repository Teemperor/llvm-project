//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/NamedType.h"
#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/Identifier.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"

#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

TEST(NamedTypeTest, StoresName) {
  IdentifierMap ids;
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};
  NamedType<Type> type(ids.get("Foo"));
  EXPECT_EQ(type.GetName().GetName(), "Foo");
  // A NamedType has no scope, so the unqualified name is the same as the name.
  EXPECT_EQ(type.GetUnqualifiedName().GetName(), "Foo");
  EXPECT_EQ(&type.GetDeclContext(context), &context.GetGlobalScope());
}
