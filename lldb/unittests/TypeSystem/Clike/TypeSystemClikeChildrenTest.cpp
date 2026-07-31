//===-- TypeSystemClikeChildrenTest.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "llvm/Testing/Support/Error.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

namespace {
struct TypeSystemClikeChildrenTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::Triple("x86_64-pc-linux-gnu"));
  Builder builder{*ts};

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }

  /// A complete record with two `int` fields, "a" at offset 0 and "b" at
  /// offset 4.
  CompilerType MakeSimpleRecord() {
    CompilerType record = builder.CreateRecordType("Pair", 8, false);
    auto *r =
        llvm::cast<RecordType>(static_cast<clike_typesystem::Type *>(record.GetOpaqueQualType()));
    builder.AddField(*r, builder.GetIdentifier("a"),
                     static_cast<clike_typesystem::Type *>(GetInt().GetOpaqueQualType()), 0);
    builder.AddField(*r, builder.GetIdentifier("b"),
                     static_cast<clike_typesystem::Type *>(GetInt().GetOpaqueQualType()), 4);
    builder.SetRecordComplete(*r);
    return record;
  }
};
} // namespace

// A record's children are its fields; each child reports its name, size, and
// offset.
TEST_F(TypeSystemClikeChildrenTest, RecordChildren) {
  CompilerType record = MakeSimpleRecord();
  auto num_children =
      ts->GetNumChildren(record.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 2u);

  std::string name;
  uint32_t byte_size = 0;
  int32_t byte_offset = 0;
  uint32_t bf_size = 0, bf_offset = 0;
  bool is_base = false, is_deref = false;
  uint64_t lang_flags = 0;
  auto child = ts->GetChildCompilerTypeAtIndex(
      record.GetOpaqueQualType(), nullptr, 1, /*transparent_pointers=*/true,
      /*omit_empty_base_classes=*/true, /*ignore_array_bounds=*/false, name,
      byte_size, byte_offset, bf_size, bf_offset, is_base, is_deref, nullptr,
      lang_flags);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_EQ(name, "b");
  EXPECT_EQ(byte_size, 4u);
  EXPECT_EQ(byte_offset, 4);
  EXPECT_FALSE(is_base);
}

// An array's children are its elements, named "[N]" with the appropriate
// offset.
TEST_F(TypeSystemClikeChildrenTest, ArrayChildren) {
  CompilerType array = builder.CreateArrayType(GetInt(), 3);
  auto num_children =
      ts->GetNumChildren(array.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 3u);

  std::string name;
  uint32_t byte_size = 0;
  int32_t byte_offset = 0;
  uint32_t bf_size = 0, bf_offset = 0;
  bool is_base = false, is_deref = false;
  uint64_t lang_flags = 0;
  auto child = ts->GetChildCompilerTypeAtIndex(
      array.GetOpaqueQualType(), nullptr, 2, /*transparent_pointers=*/true,
      true, false, name, byte_size, byte_offset, bf_size, bf_offset, is_base,
      is_deref, nullptr, lang_flags);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_EQ(name, "[2]");
  EXPECT_EQ(byte_offset, 8);
}

// A pointer to a complete aggregate is transparent when asked (mirroring
// TypeSystemClang): its children are the pointee's members, not a single
// deref child.
TEST_F(TypeSystemClikeChildrenTest, TransparentPointerToCompleteAggregate) {
  CompilerType record = MakeSimpleRecord();
  CompilerType ptr = builder.CreatePointerType(record);

  auto num_children =
      ts->GetNumChildren(ptr.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 2u);

  std::string name;
  uint32_t byte_size = 0;
  int32_t byte_offset = 0;
  uint32_t bf_size = 0, bf_offset = 0;
  bool is_base = false, is_deref = false;
  uint64_t lang_flags = 0;
  auto child = ts->GetChildCompilerTypeAtIndex(
      ptr.GetOpaqueQualType(), nullptr, 0, /*transparent_pointers=*/true, true,
      false, name, byte_size, byte_offset, bf_size, bf_offset, is_base,
      is_deref, nullptr, lang_flags);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_EQ(name, "a");
  EXPECT_FALSE(is_deref);
}

// A pointer to a scalar (non-aggregate) always has exactly one (deref) child,
// regardless of the transparent_pointers flag.
TEST_F(TypeSystemClikeChildrenTest, PointerToScalarHasSingleDerefChild) {
  CompilerType ptr = builder.CreatePointerType(GetInt());
  auto num_children =
      ts->GetNumChildren(ptr.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 1u);

  std::string name;
  uint32_t byte_size = 0;
  int32_t byte_offset = 0;
  uint32_t bf_size = 0, bf_offset = 0;
  bool is_base = false, is_deref = false;
  uint64_t lang_flags = 0;
  auto child = ts->GetChildCompilerTypeAtIndex(
      ptr.GetOpaqueQualType(), nullptr, 0, /*transparent_pointers=*/true, true,
      false, name, byte_size, byte_offset, bf_size, bf_offset, is_base,
      is_deref, nullptr, lang_flags);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_TRUE(is_deref);
  EXPECT_EQ(child->GetOpaqueQualType(), GetInt().GetOpaqueQualType());
}

// Even a pointer to a complete aggregate keeps its single non-transparent
// deref child (idx 0) when transparent_pointers is false -- this is what the
// DIL `ptr->member` navigation relies on.
TEST_F(TypeSystemClikeChildrenTest, NonTransparentPointerDerefsToWholeAggregate) {
  CompilerType record = MakeSimpleRecord();
  CompilerType ptr = builder.CreatePointerType(record);

  std::string name;
  uint32_t byte_size = 0;
  int32_t byte_offset = 0;
  uint32_t bf_size = 0, bf_offset = 0;
  bool is_base = false, is_deref = false;
  uint64_t lang_flags = 0;
  auto child = ts->GetChildCompilerTypeAtIndex(
      ptr.GetOpaqueQualType(), nullptr, 0, /*transparent_pointers=*/false, true,
      false, name, byte_size, byte_offset, bf_size, bf_offset, is_base,
      is_deref, nullptr, lang_flags);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_TRUE(is_deref);
  EXPECT_EQ(child->GetOpaqueQualType(), record.GetOpaqueQualType());
}

// GetIndexOfChildWithName finds a field by name on a plain record, and (by
// forwarding through the pointer) on a pointer to a record too.
TEST_F(TypeSystemClikeChildrenTest, GetIndexOfChildWithName) {
  CompilerType record = MakeSimpleRecord();
  auto idx =
      ts->GetIndexOfChildWithName(record.GetOpaqueQualType(), "b", true);
  ASSERT_THAT_EXPECTED(idx, llvm::Succeeded());
  EXPECT_EQ(*idx, 1u);

  CompilerType ptr = builder.CreatePointerType(record);
  auto idx_through_ptr =
      ts->GetIndexOfChildWithName(ptr.GetOpaqueQualType(), "b", true);
  ASSERT_THAT_EXPECTED(idx_through_ptr, llvm::Succeeded());
  EXPECT_EQ(*idx_through_ptr, 1u);
}

// GetIndexOfChildWithName reports an error for a name that doesn't exist.
TEST_F(TypeSystemClikeChildrenTest, GetIndexOfChildWithNameNotFound) {
  CompilerType record = MakeSimpleRecord();
  auto idx = ts->GetIndexOfChildWithName(record.GetOpaqueQualType(),
                                         "nonexistent", true);
  EXPECT_THAT_EXPECTED(idx, llvm::Failed());
}

// An empty base class is skipped (and does not consume a child index) when
// omit_empty_base_classes is set; the derived class's own field then becomes
// child 0.
TEST_F(TypeSystemClikeChildrenTest, EmptyBaseClassOmitted) {
  CompilerType empty_base =
      builder.CreateRecordType("EmptyBase", 1, true);
  builder.SetRecordComplete(*llvm::cast<ClassType>(
      static_cast<clike_typesystem::Type *>(empty_base.GetOpaqueQualType())));

  CompilerType derived =
      builder.CreateRecordType("Derived", 4, true);
  auto *derived_class = llvm::cast<ClassType>(
      static_cast<clike_typesystem::Type *>(derived.GetOpaqueQualType()));
  builder.AddBaseClass(
      *derived_class, static_cast<clike_typesystem::Type *>(empty_base.GetOpaqueQualType()), 0);
  builder.AddField(*derived_class, builder.GetIdentifier("x"),
                   static_cast<clike_typesystem::Type *>(GetInt().GetOpaqueQualType()), 0);
  builder.SetRecordComplete(*derived_class);

  auto num_children_omit =
      ts->GetNumChildren(derived.GetOpaqueQualType(), true, nullptr);
  ASSERT_THAT_EXPECTED(num_children_omit, llvm::Succeeded());
  EXPECT_EQ(*num_children_omit, 1u);

  auto num_children_keep =
      ts->GetNumChildren(derived.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children_keep, llvm::Succeeded());
  EXPECT_EQ(*num_children_keep, 2u);
}
