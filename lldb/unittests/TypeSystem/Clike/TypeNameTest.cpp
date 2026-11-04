//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/TypeName.h"
#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"
#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeC.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

namespace {
struct TypeNameTest : public testing::Test {
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};

  Type *Int() { return context.GetBuiltinType(BuiltinKind::Int); }
  Type *Char() { return context.GetBuiltinType(BuiltinKind::Char); }
  Type *Void() { return context.GetBuiltinType(BuiltinKind::Void); }

  std::string Canonical(Type *t) {
    return BuildCanonicalName(t, /*base_only=*/false);
  }
  std::string Display(Type *t) { return BuildDisplayName(t); }
};
} // namespace

// A builtin renders as its plain spelling.
TEST_F(TypeNameTest, Builtin) {
  EXPECT_EQ(Canonical(Int()), "int");
  EXPECT_EQ(Canonical(Void()), "void");
  EXPECT_EQ(Display(Int()), "int");
}

// A typedef reports its own name rather than what it aliases.
TEST_F(TypeNameTest, Typedef) {
  TypedefType *alias = context.CreateTypedefType("my_int", TypeRef(*Int()));
  EXPECT_EQ(Canonical(alias), "my_int");
  EXPECT_EQ(Display(alias), "my_int");
}
