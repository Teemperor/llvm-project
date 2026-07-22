//===-- TypeSystemCppRecordQueriesTest.cpp -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Cpp/Builder.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

namespace {
struct TypeSystemCppRecordQueriesTest : public testing::Test {
  std::shared_ptr<TypeSystemCpp> ts = std::make_shared<TypeSystemCpp>(
      "test", llvm::Triple("x86_64-pc-linux-gnu"));
  Builder builder{*ts};

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }

  /// Create a complete record with one `int` field named `x`.
  CompilerType MakeRecordWithField(llvm::StringRef record_name,
                                   llvm::StringRef field_name) {
    CompilerType record =
        builder.CreateRecordType(record_name, 4, false);
    auto *r =
        llvm::cast<RecordType>(static_cast<cpp_typesystem::Type *>(record.GetOpaqueQualType()));
    builder.AddField(*r, builder.GetIdentifier(field_name),
                     static_cast<cpp_typesystem::Type *>(GetInt().GetOpaqueQualType()), 0);
    builder.SetRecordComplete(*r);
    return record;
  }
};
} // namespace

// GetNumFields/GetFieldAtIndex report a completed record's data members with
// their name and bit offset.
TEST_F(TypeSystemCppRecordQueriesTest, FieldsBasic) {
  CompilerType record = MakeRecordWithField("Foo", "x");
  EXPECT_EQ(ts->GetNumFields(record.GetOpaqueQualType()), 1u);

  std::string name;
  uint64_t bit_offset = 0;
  uint32_t bitfield_size = 0;
  bool is_bitfield = false;
  CompilerType field_type =
      ts->GetFieldAtIndex(record.GetOpaqueQualType(), 0, name, &bit_offset,
                          &bitfield_size, &is_bitfield);
  EXPECT_EQ(name, "x");
  EXPECT_EQ(bit_offset, 0u);
  EXPECT_FALSE(is_bitfield);
  EXPECT_EQ(field_type.GetOpaqueQualType(), GetInt().GetOpaqueQualType());
}

// A bitfield's bit offset combines the storage-unit byte offset and the
// bitfield's own bit offset within it, and reports is_bitfield.
TEST_F(TypeSystemCppRecordQueriesTest, BitfieldOffsets) {
  CompilerType record = builder.CreateRecordType("BF", 4, false);
  auto *r =
      llvm::cast<RecordType>(static_cast<cpp_typesystem::Type *>(record.GetOpaqueQualType()));
  builder.AddField(*r, builder.GetIdentifier("flag"),
                   static_cast<cpp_typesystem::Type *>(GetInt().GetOpaqueQualType()),
                   /*byte_offset=*/0, /*bitfield_bit_size=*/1,
                   /*bitfield_bit_offset=*/3);
  builder.SetRecordComplete(*r);

  std::string name;
  uint64_t bit_offset = 0;
  uint32_t bitfield_size = 0;
  bool is_bitfield = false;
  ts->GetFieldAtIndex(record.GetOpaqueQualType(), 0, name, &bit_offset,
                      &bitfield_size, &is_bitfield);
  EXPECT_TRUE(is_bitfield);
  EXPECT_EQ(bitfield_size, 1u);
  EXPECT_EQ(bit_offset, 3u);
}

// GetNumDirectBaseClasses/GetDirectBaseClassAtIndex report a ClassType's base
// classes, with the bit offset derived from the base's byte offset.
TEST_F(TypeSystemCppRecordQueriesTest, DirectBaseClasses) {
  CompilerType base = builder.CreateRecordType("Base", 4, true);
  builder.SetRecordComplete(
      *llvm::cast<ClassType>(static_cast<cpp_typesystem::Type *>(base.GetOpaqueQualType())));

  CompilerType derived =
      builder.CreateRecordType("Derived", 8, true);
  auto *derived_class = llvm::cast<ClassType>(
      static_cast<cpp_typesystem::Type *>(derived.GetOpaqueQualType()));
  builder.AddBaseClass(*derived_class,
                       static_cast<cpp_typesystem::Type *>(base.GetOpaqueQualType()),
                       /*byte_offset=*/4);
  builder.SetRecordComplete(*derived_class);

  EXPECT_EQ(ts->GetNumDirectBaseClasses(derived.GetOpaqueQualType()), 1u);
  uint32_t bit_offset = 0;
  CompilerType base_from_ts = ts->GetDirectBaseClassAtIndex(
      derived.GetOpaqueQualType(), 0, &bit_offset);
  EXPECT_EQ(base_from_ts.GetOpaqueQualType(), base.GetOpaqueQualType());
  EXPECT_EQ(bit_offset, 32u);
}

// A plain (non-C++) struct never has base classes, even after completion.
TEST_F(TypeSystemCppRecordQueriesTest, StructHasNoBaseClasses) {
  CompilerType record = MakeRecordWithField("PlainStruct", "x");
  EXPECT_EQ(ts->GetNumDirectBaseClasses(record.GetOpaqueQualType()), 0u);
}

// TypeSystemCpp does not model virtual base classes through the (Itanium
// dedicated-vtable-slot) "virtual base class" API; it always reports zero,
// even for a class with a virtual base (see the vbase_offset_offset note on
// BaseClass -- virtual bases are still direct base classes, just with runtime
// offsets instead of constant ones).
TEST_F(TypeSystemCppRecordQueriesTest, NoVirtualBaseClasses) {
  CompilerType record = MakeRecordWithField("Foo", "x");
  EXPECT_EQ(ts->GetNumVirtualBaseClasses(record.GetOpaqueQualType()), 0u);
}

// GetStaticFieldWithName finds a static data member by name and returns an
// (invalid) empty decl when there is none by that name.
TEST_F(TypeSystemCppRecordQueriesTest, StaticFieldLookup) {
  CompilerType record = builder.CreateRecordType("Foo", 4, true);
  auto *r =
      llvm::cast<ClassType>(static_cast<cpp_typesystem::Type *>(record.GetOpaqueQualType()));
  builder.AddStaticDataMember(*r, "s_count",
                              static_cast<cpp_typesystem::Type *>(GetInt().GetOpaqueQualType()),
                              "_ZN3Foo7s_countE", std::nullopt);
  builder.SetRecordComplete(*r);

  CompilerDecl decl =
      ts->GetStaticFieldWithName(record.GetOpaqueQualType(), "s_count");
  EXPECT_TRUE(decl.IsValid());
  EXPECT_EQ(ts->DeclGetName(decl.GetOpaqueDecl()).GetStringRef(), "s_count");

  CompilerDecl missing =
      ts->GetStaticFieldWithName(record.GetOpaqueQualType(), "nonexistent");
  EXPECT_FALSE(missing.IsValid());
}

// IsAnonymousType recognizes a record marked as an anonymous struct/union
// (its members are injected into the enclosing scope), not an ordinary named
// or unnamed-but-not-anonymous record.
TEST_F(TypeSystemCppRecordQueriesTest, IsAnonymousType) {
  CompilerType record = builder.CreateRecordType("", 4, false,
                                                 /*is_union=*/true);
  auto *r =
      llvm::cast<RecordType>(static_cast<cpp_typesystem::Type *>(record.GetOpaqueQualType()));
  builder.SetRecordAnonymousStructOrUnion(*r);
  EXPECT_TRUE(ts->IsAnonymousType(record.GetOpaqueQualType()));

  CompilerType named = MakeRecordWithField("Named", "x");
  EXPECT_FALSE(ts->IsAnonymousType(named.GetOpaqueQualType()));
}
