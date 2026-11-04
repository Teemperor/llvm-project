//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/TypeC.h"
#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"
#include "Plugins/TypeSystem/Clike/Type.h"

#include "llvm/ADT/APInt.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

namespace {
struct TypeCTest : public testing::Test {
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};

  Type *Int() { return context.GetBuiltinType(BuiltinKind::Int); }
  Type *Void() { return context.GetBuiltinType(BuiltinKind::Void); }
};
} // namespace

// A typedef is sugar: it reports its own name and type class but forwards
// layout to what it aliases, and desugars through to the canonical type.
TEST_F(TypeCTest, Typedef) {
  TypedefType *alias = context.CreateTypedefType("my_int", TypeRef(*Int()));
  EXPECT_EQ(alias->GetName().GetName(), "my_int");
  EXPECT_EQ(alias->GetUnderlyingType(), Int());
  EXPECT_EQ(alias->GetByteSize(), Int()->GetByteSize());
  EXPECT_EQ(alias->GetTypeClass(), lldb::eTypeClassTypedef);
  EXPECT_EQ(alias->Desugar(), Int());
  EXPECT_EQ(alias->GetTypedefedType(), Int());
  EXPECT_TRUE(llvm::isa<SugarType>(static_cast<Type *>(alias)));
}
