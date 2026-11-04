//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

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
struct ContextTest : public testing::Test {
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};
};
} // namespace

// GetBuiltinType by (name, encoding, size) prefers the shared canonical
// instance when the attributes match a known builtin.
TEST_F(ContextTest, GetBuiltinTypeReturnsCanonical) {
  BuiltinType *a = context.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                          lldb::eFormatDecimal);
  BuiltinType *b = context.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                          lldb::eFormatDecimal);
  EXPECT_EQ(a, b);
  EXPECT_EQ(a, context.GetBuiltinType(BuiltinKind::Int));
}

// Attributes that don't match any enumerated builtin fall back to a bespoke,
// Context-owned type (distinct from any canonical instance).
TEST_F(ContextTest, GetBuiltinTypeBespokeFallback) {
  BuiltinType *bespoke = context.GetBuiltinType(
      "unusual_int_type", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  ASSERT_NE(bespoke, nullptr);
  EXPECT_NE(bespoke, context.GetBuiltinType(BuiltinKind::Int));
  EXPECT_EQ(bespoke->GetName().GetName(), "unusual_int_type");
  EXPECT_FALSE(bespoke->GetBuiltinKind().has_value());
}

// The global scope is a declaration context without a parent.
TEST_F(ContextTest, GlobalScope) {
  const DeclarationContext &global = context.GetGlobalScope();
  EXPECT_EQ(global.GetKind(), DeclarationContext::Kind::Global);
  EXPECT_TRUE(llvm::isa<GlobalScope>(global));
  EXPECT_EQ(global.GetParent(), nullptr);
}

// A type without a scope of its own (e.g. a builtin) is declared in the global
// scope of the Context that owns it.
TEST_F(ContextTest, UnscopedTypeIsInGlobalScope) {
  BuiltinType *type = context.GetBuiltinType(BuiltinKind::Int);
  EXPECT_EQ(&type->GetDeclContext(), &context.GetGlobalScope());
}
