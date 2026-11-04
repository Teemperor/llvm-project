//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/RecordType.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

// A record is an aggregate and starts out as a forward declaration.
TEST(RecordTypeTest, RecordDefaults) {
  RecordType record;
  EXPECT_TRUE(record.IsAggregate());
  EXPECT_TRUE(record.IsRecord());
  EXPECT_EQ(record.GetMinimumLanguage(), lldb::eLanguageTypeC_plus_plus);
  EXPECT_FALSE(record.IsComplete());
  EXPECT_FALSE(record.GetExplicitAlignmentInBits().has_value());
  EXPECT_FALSE(record.IsUnion());
  EXPECT_FALSE(record.IsClassKeyword());
  EXPECT_FALSE(record.IsAnonymousStructOrUnion());
  EXPECT_EQ(record.GetAnonymousParent(), nullptr);
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
  EXPECT_FALSE(RecordType().GetByteSize().has_value());
  EXPECT_EQ(RecordType(Identifier(), DeclScope(), 8).GetByteSize(),
            std::optional<uint64_t>(8));
}
