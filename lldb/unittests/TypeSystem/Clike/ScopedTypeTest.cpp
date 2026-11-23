//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/ScopedType.h"
#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/Identifier.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"

#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

TEST(ScopedTypeTest, StoresNames) {
  IdentifierMap ids;
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};
  ScopedType<Type> type(ids.get("std::vector<int, std::allocator<int> >"),
                        context.GetGlobalScope(), ids.get("vector<int>"));
  EXPECT_EQ(type.GetName().GetName(), "std::vector<int, std::allocator<int> >");
  EXPECT_EQ(type.GetUnqualifiedName().GetName(), "vector<int>");
  // Declared in the global scope.
  EXPECT_EQ(&type.GetDeclContext(), &context.GetGlobalScope());
}
