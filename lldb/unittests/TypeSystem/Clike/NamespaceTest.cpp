//===-- NamespaceTest.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"
#include "Plugins/TypeSystem/Clike/Namespace.h"

#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

namespace {
struct NamespaceTest : public testing::Test {
  LanguageOpts opts{llvm::Triple("x86_64-pc-linux-gnu")};
  Context context{opts};
};
} // namespace

// A top-level (non-nested) namespace has a null parent and reports its name.
TEST_F(NamespaceTest, TopLevel) {
  const Namespace *ns =
      context.GetNamespace(context.GetIdentifier("std"), nullptr,
                           /*is_inline=*/false);
  ASSERT_NE(ns, nullptr);
  EXPECT_EQ(ns->GetName().GetName(), "std");
  EXPECT_EQ(ns->GetParent(), nullptr);
  EXPECT_FALSE(ns->IsInline());
}

// Requesting the same (parent, name, is_inline) twice returns the identical
// Namespace instance rather than creating a duplicate.
TEST_F(NamespaceTest, Interned) {
  const Namespace *a = context.GetNamespace(context.GetIdentifier("std"),
                                            nullptr, /*is_inline=*/false);
  const Namespace *b = context.GetNamespace(context.GetIdentifier("std"),
                                            nullptr, /*is_inline=*/false);
  EXPECT_EQ(a, b);
}

// Namespaces that differ in name, parent, or inline-ness are distinct even if
// the other attributes match.
TEST_F(NamespaceTest, DistinctByAttributes) {
  const Namespace *std_ns = context.GetNamespace(
      context.GetIdentifier("std"), nullptr, /*is_inline=*/false);
  const Namespace *other_ns = context.GetNamespace(
      context.GetIdentifier("other"), nullptr, /*is_inline=*/false);
  EXPECT_NE(std_ns, other_ns);

  const Namespace *inline_ns = context.GetNamespace(
      context.GetIdentifier("std"), nullptr, /*is_inline=*/true);
  EXPECT_NE(std_ns, inline_ns);

  const Namespace *nested_ns = context.GetNamespace(
      context.GetIdentifier("std"), std_ns, /*is_inline=*/false);
  EXPECT_NE(std_ns, nested_ns);
  EXPECT_EQ(nested_ns->GetParent(), std_ns);
}

// An anonymous namespace is interned with an empty name and reports as such.
TEST_F(NamespaceTest, Anonymous) {
  const Namespace *ns = context.GetNamespace(context.GetIdentifier(""),
                                             nullptr, /*is_inline=*/false);
  ASSERT_NE(ns, nullptr);
  EXPECT_TRUE(ns->IsAnonymous());
}

// A namespace with a non-empty name is not anonymous.
TEST_F(NamespaceTest, NotAnonymous) {
  const Namespace *ns = context.GetNamespace(context.GetIdentifier("std"),
                                             nullptr, /*is_inline=*/false);
  EXPECT_FALSE(ns->IsAnonymous());
}
