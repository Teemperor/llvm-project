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
#include "llvm/Testing/Support/Error.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike;

namespace {
struct TypeSystemClikeChildrenTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::cantFail(clike::LanguageOpts::Create(
                  llvm::Triple("x86_64-pc-linux-gnu"))));
  Builder builder{*ts};

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }

  /// A complete record with two `int` fields, "a" at offset 0 and "b" at
  /// offset 4.
  CompilerType MakeSimpleRecord() {
    CompilerType record =
        builder.CreateRecordType(builder.GetGlobalScope(), "Pair", 8);
    auto *r = llvm::cast<RecordType>(
        static_cast<clike::Type *>(record.GetOpaqueQualType()));
    builder.AddField(*r, builder.GetIdentifier("a"),
                     static_cast<clike::Type *>(GetInt().GetOpaqueQualType()),
                     0);
    builder.AddField(*r, builder.GetIdentifier("b"),
                     static_cast<clike::Type *>(GetInt().GetOpaqueQualType()),
                     4);
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

  auto child = ts->GetChildAtIndex(
      record.GetOpaqueQualType(), nullptr, 1, /*transparent_pointers=*/true,
      /*omit_empty_base_classes=*/true, /*ignore_array_bounds=*/false, nullptr);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_EQ(child->name, "b");
  EXPECT_EQ(child->byte_size, 4u);
  EXPECT_EQ(child->byte_offset, 4);
  EXPECT_FALSE(child->is_base_class);
}

// An array's children are its elements, named "[N]" with the appropriate
// offset.
TEST_F(TypeSystemClikeChildrenTest, ArrayChildren) {
  CompilerType array = builder.CreateArrayType(GetInt(), 3);
  auto num_children =
      ts->GetNumChildren(array.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 3u);

  auto child =
      ts->GetChildAtIndex(array.GetOpaqueQualType(), nullptr, 2,
                          /*transparent_pointers=*/true, true, false, nullptr);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_EQ(child->name, "[2]");
  EXPECT_EQ(child->byte_offset, 8);
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

  auto child =
      ts->GetChildAtIndex(ptr.GetOpaqueQualType(), nullptr, 0,
                          /*transparent_pointers=*/true, true, false, nullptr);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_EQ(child->name, "a");
  EXPECT_FALSE(child->is_deref_of_parent);
}

// A pointer to a scalar (non-aggregate) always has exactly one (deref) child,
// regardless of the transparent_pointers flag.
TEST_F(TypeSystemClikeChildrenTest, PointerToScalarHasSingleDerefChild) {
  CompilerType ptr = builder.CreatePointerType(GetInt());
  auto num_children =
      ts->GetNumChildren(ptr.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 1u);

  auto child =
      ts->GetChildAtIndex(ptr.GetOpaqueQualType(), nullptr, 0,
                          /*transparent_pointers=*/true, true, false, nullptr);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_TRUE(child->is_deref_of_parent);
  EXPECT_EQ(child->type.GetOpaqueQualType(), GetInt().GetOpaqueQualType());
}

// `void *` is the one pointer with no deref child: its pointee is the `void`
// builtin (a TypeRef always names a type), so this has to be recognized as
// void rather than as an absent pointee.
TEST_F(TypeSystemClikeChildrenTest, VoidPointerHasNoChildren) {
  CompilerType ptr = builder.CreatePointerType(builder.GetVoidType());
  auto num_children =
      ts->GetNumChildren(ptr.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 0u);

  // Its pointee is still reported (as `void`), matching TypeSystemClang.
  EXPECT_TRUE(ts->GetPointeeType(ptr.GetOpaqueQualType()).IsVoidType());

  auto child =
      ts->GetChildAtIndex(ptr.GetOpaqueQualType(), nullptr, 0,
                          /*transparent_pointers=*/true, true, false, nullptr);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_FALSE(child->type.IsValid());
}

// Even a pointer to a complete aggregate keeps its single non-transparent
// deref child (idx 0) when transparent_pointers is false -- this is what the
// DIL `ptr->member` navigation relies on.
TEST_F(TypeSystemClikeChildrenTest,
       NonTransparentPointerDerefsToWholeAggregate) {
  CompilerType record = MakeSimpleRecord();
  CompilerType ptr = builder.CreatePointerType(record);

  auto child =
      ts->GetChildAtIndex(ptr.GetOpaqueQualType(), nullptr, 0,
                          /*transparent_pointers=*/false, true, false, nullptr);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_TRUE(child->is_deref_of_parent);
  EXPECT_EQ(child->type.GetOpaqueQualType(), record.GetOpaqueQualType());
}

// GetIndexOfChildWithName finds a field by name on a plain record, and (by
// forwarding through the pointer) on a pointer to a record too.
TEST_F(TypeSystemClikeChildrenTest, GetIndexOfChildWithName) {
  CompilerType record = MakeSimpleRecord();
  auto idx = ts->GetIndexOfChildWithName(record.GetOpaqueQualType(), "b", true);
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
