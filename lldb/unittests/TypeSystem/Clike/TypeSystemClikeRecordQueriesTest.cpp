//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "llvm/Support/Error.h"
#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike;

namespace {
struct TypeSystemClikeRecordQueriesTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::cantFail(clike::LanguageOpts::Create(
                  llvm::Triple("x86_64-pc-linux-gnu"))));
  Builder builder{*ts};

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }

  /// Create a complete record with one `int` field named `x`.
  CompilerType MakeRecordWithField(llvm::StringRef record_name,
                                   llvm::StringRef field_name) {
    CompilerType record = builder.CreateRecordType(record_name, 4);
    auto *r = llvm::cast<RecordType>(
        static_cast<clike::Type *>(record.GetOpaqueQualType()));
    builder.AddField(*r, builder.GetIdentifier(field_name),
                     static_cast<clike::Type *>(GetInt().GetOpaqueQualType()),
                     0);
    builder.SetRecordComplete(*r);
    return record;
  }
};
} // namespace

// GetNumFields/GetFieldAtIndex report a completed record's data members with
// their name and bit offset.
TEST_F(TypeSystemClikeRecordQueriesTest, FieldsBasic) {
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
TEST_F(TypeSystemClikeRecordQueriesTest, BitfieldOffsets) {
  CompilerType record = builder.CreateRecordType("BF", 4);
  auto *r = llvm::cast<RecordType>(
      static_cast<clike::Type *>(record.GetOpaqueQualType()));
  builder.AddField(*r, builder.GetIdentifier("flag"),
                   static_cast<clike::Type *>(GetInt().GetOpaqueQualType()),
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

// A plain struct never has base classes, even after completion.
TEST_F(TypeSystemClikeRecordQueriesTest, StructHasNoBaseClasses) {
  CompilerType record = MakeRecordWithField("PlainStruct", "x");
  EXPECT_EQ(ts->GetNumDirectBaseClasses(record.GetOpaqueQualType()), 0u);
}

// TypeSystemClike does not model virtual base classes through the (Itanium
// dedicated-vtable-slot) "virtual base class" API; it always reports zero,
// even for a class with a virtual base (see the vbase_offset_offset note on
// BaseClass -- virtual bases are still direct base classes, just with runtime
// offsets instead of constant ones).
TEST_F(TypeSystemClikeRecordQueriesTest, NoVirtualBaseClasses) {
  CompilerType record = MakeRecordWithField("Foo", "x");
  EXPECT_EQ(ts->GetNumVirtualBaseClasses(record.GetOpaqueQualType()), 0u);
}

// IsAnonymousType recognizes a record marked as an anonymous struct/union
// (its members are injected into the enclosing scope), not an ordinary named
// or unnamed-but-not-anonymous record.
TEST_F(TypeSystemClikeRecordQueriesTest, IsAnonymousType) {
  CompilerType record = builder.CreateRecordType("", 4, /*is_union=*/true);
  auto *r = llvm::cast<RecordType>(
      static_cast<clike::Type *>(record.GetOpaqueQualType()));
  builder.SetRecordAnonymousStructOrUnion(*r);
  EXPECT_TRUE(ts->IsAnonymousType(record.GetOpaqueQualType()));

  CompilerType named = MakeRecordWithField("Named", "x");
  EXPECT_FALSE(ts->IsAnonymousType(named.GetOpaqueQualType()));
}

// GetTypeBitAlign reports the alignment of a record's most aligned member,
// also for an array of the record.
TEST_F(TypeSystemClikeRecordQueriesTest, TypeBitAlign) {
  CompilerType ld = builder.GetBuiltinType(
      "long double", 16, lldb::eEncodingIEEE754, lldb::eFormatFloat);
  CompilerType record = builder.CreateRecordType("LD", 16);
  auto *r = llvm::cast<RecordType>(
      static_cast<clike::Type *>(record.GetOpaqueQualType()));
  builder.AddField(*r, builder.GetIdentifier("ld"),
                   static_cast<clike::Type *>(ld.GetOpaqueQualType()), 0);
  builder.SetRecordComplete(*r);

  EXPECT_EQ(ts->GetTypeBitAlign(record.GetOpaqueQualType(), nullptr), 128u);
  EXPECT_EQ(record.GetArrayType(2).GetTypeBitAlign(nullptr), 128u);
  EXPECT_EQ(GetInt().GetTypeBitAlign(nullptr), 32u);
}
