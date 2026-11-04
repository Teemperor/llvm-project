//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/RecordType.h"
#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"

#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

// A record is an aggregate and starts out as a forward declaration.
TEST(RecordTypeTest, RecordDefaults) {
  GlobalScope global;
  RecordType record(Identifier(), global, Identifier());
  EXPECT_TRUE(record.IsAggregate());
  EXPECT_TRUE(record.IsRecord());
  EXPECT_EQ(record.GetMinimumLanguage(), lldb::eLanguageTypeC_plus_plus);
  EXPECT_FALSE(record.IsComplete());
  EXPECT_FALSE(record.GetExplicitAlignmentInBits().has_value());
  EXPECT_FALSE(record.IsUnion());
  EXPECT_FALSE(record.IsClassKeyword());
  EXPECT_FALSE(record.AreMemberFunctionsParsed());
  EXPECT_EQ(record.GetArgPassingKind(),
            RecordType::ArgPassingKind::Unspecified);
  EXPECT_EQ(record.GetTypeInfo(),
            uint32_t(lldb::eTypeHasChildren | lldb::eTypeIsStructUnion));
  // The C++-only accessors default to "none" here; ClassType overrides them.
  EXPECT_FALSE(record.IsTemplateInstantiation());
  EXPECT_EQ(record.GetNumTemplateArguments(), 0u);
  EXPECT_EQ(record.GetTemplateArgumentAtIndex(0), nullptr);
  EXPECT_EQ(record.GetNumMemberFunctions(), 0u);
  EXPECT_EQ(record.GetMemberFunctionAtIndex(0), nullptr);
  EXPECT_EQ(record.GetNumStaticDataMembers(), 0u);
  EXPECT_EQ(record.GetStaticDataMemberAtIndex(0), nullptr);
  EXPECT_EQ(record.GetNumNestedTypes(), 0u);
  EXPECT_EQ(record.GetNestedTypeWithName("anything"), nullptr);
}

// A record's size is unknown unless one was given.
TEST(RecordTypeTest, ByteSize) {
  GlobalScope global;
  EXPECT_FALSE(
      RecordType(Identifier(), global, Identifier()).GetByteSize().has_value());
  EXPECT_EQ(RecordType(Identifier(), global, Identifier(), 8).GetByteSize(),
            std::optional<uint64_t>(8));
}

// A record is the declaration context of the types nested in it. Its parent is
// the scope the record itself is declared in.
TEST(RecordTypeTest, IsDeclarationContext) {
  IdentifierMap ids;
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};
  RecordType outer(ids.get("Outer"), context.GetGlobalScope(), ids.get("Outer"),
                   8);
  RecordType inner(ids.get("Outer::Inner"), outer, ids.get("Inner"), 4);

  const DeclarationContext &ctx = inner.GetDeclContext();
  ASSERT_EQ(&ctx, &outer);
  EXPECT_EQ(ctx.GetKind(), DeclarationContext::Kind::Record);
  EXPECT_EQ(llvm::dyn_cast<RecordType>(&ctx), &outer);
  // `Outer` is declared in the global scope, which is not part of the parent
  // chain.
  EXPECT_EQ(&outer.GetDeclContext(), &context.GetGlobalScope());
  EXPECT_EQ(ctx.GetParent(), nullptr);
}
