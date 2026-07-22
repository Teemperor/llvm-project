//===-- ContextTest.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Cpp/Context.h"
#include "Plugins/TypeSystem/Cpp/LanguageOpts.h"
#include "Plugins/TypeSystem/Cpp/Type.h"

#include "llvm/Support/Casting.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::cpp_typesystem;

namespace {
struct ContextTest : public testing::Test {
  LanguageOpts opts{llvm::Triple("x86_64-pc-linux-gnu")};
  Context context{opts};
};
} // namespace

// A plain (non-C++) record is created as a StructType, not a ClassType, and
// starts out incomplete.
TEST_F(ContextTest, CreateStructRecord) {
  RecordType *r = context.CreateRecordType("Foo", /*byte_size=*/4,
                                           /*is_cpp_class=*/false);
  ASSERT_NE(r, nullptr);
  EXPECT_TRUE(llvm::isa<StructType>(r));
  EXPECT_FALSE(llvm::isa<ClassType>(r));
  EXPECT_EQ(r->GetName().GetName(), "Foo");
  EXPECT_EQ(r->GetByteSize(), 4u);
  EXPECT_FALSE(r->IsComplete());
  EXPECT_FALSE(r->IsUnion());
}

// A C++ record is created as a ClassType (which can carry base classes), even
// though it is also still a RecordType.
TEST_F(ContextTest, CreateClassRecord) {
  RecordType *r = context.CreateRecordType("Foo", /*byte_size=*/8,
                                           /*is_cpp_class=*/true);
  EXPECT_TRUE(llvm::isa<ClassType>(r));
  EXPECT_TRUE(llvm::isa<RecordType>(r));
}

// The is_union flag is threaded through to the created record.
TEST_F(ContextTest, CreateUnionRecord) {
  RecordType *r =
      context.CreateRecordType("U", /*byte_size=*/4, /*is_cpp_class=*/false,
                               /*is_union=*/true);
  EXPECT_TRUE(r->IsUnion());
}

// GetBuiltinType by (name, encoding, size) prefers the shared canonical
// instance when the attributes match a known builtin.
TEST_F(ContextTest, GetBuiltinTypeReturnsCanonical) {
  BuiltinType *a =
      context.GetBuiltinType("int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  BuiltinType *b =
      context.GetBuiltinType("int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
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

// Pointer types are uniqued by (pointee, is_block): forming the same pointer
// twice returns the identical instance.
TEST_F(ContextTest, PointerTypesAreUniqued) {
  Type *record = context.CreateRecordType("Foo", 4, false);
  PointerType *p1 = context.CreatePointerType(TypeRef(context, record));
  PointerType *p2 = context.CreatePointerType(TypeRef(context, record));
  EXPECT_EQ(p1, p2);
  EXPECT_EQ(p1->GetPointeeType(), record);
  EXPECT_EQ(p1->GetByteSize(), 8u);
}

// A block pointer and a plain pointer to the same pointee are distinct types
// (the block pointer is a separate BlockPointerType kind).
TEST_F(ContextTest, BlockPointerDistinctFromPlainPointer) {
  Type *record = context.CreateRecordType("Foo", 4, false);
  PointerType *plain = context.CreatePointerType(TypeRef(context, record));
  BlockPointerType *block =
      context.CreateBlockPointerType(TypeRef(context, record));
  EXPECT_NE(static_cast<Type *>(plain), static_cast<Type *>(block));
  EXPECT_TRUE(llvm::isa<BlockPointerType>(block));
  EXPECT_FALSE(llvm::isa<BlockPointerType>(plain));
  // A BlockPointerType is still a PointerType.
  EXPECT_TRUE(llvm::isa<PointerType>(block));
}

// A pointer to an empty (void) TypeRef models `void *`.
TEST_F(ContextTest, VoidPointer) {
  PointerType *p = context.CreatePointerType(TypeRef());
  EXPECT_EQ(p->GetPointeeType(), nullptr);
  EXPECT_EQ(p->GetByteSize(), 8u);
}

// A typedef inherits the byte size of the type it aliases.
TEST_F(ContextTest, TypedefInheritsByteSize) {
  Type *record = context.CreateRecordType("Foo", 16, false);
  TypedefType *td =
      context.CreateTypedefType("FooAlias", TypeRef(context, record));
  EXPECT_EQ(td->GetByteSize(), 16u);
  EXPECT_EQ(td->GetUnderlyingType(), record);
}

// An array's byte size is element size times element count, when both are
// known; an array of unknown bound has no byte size of its own.
TEST_F(ContextTest, ArrayByteSizeComputed) {
  BuiltinType *elem =
      context.GetBuiltinType("int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  ArrayType *bounded =
      context.CreateArrayType(TypeRef(context, elem), /*num_elements=*/10);
  EXPECT_EQ(bounded->GetByteSize(), 40u);

  ArrayType *unbounded =
      context.CreateArrayType(TypeRef(context, elem), std::nullopt);
  EXPECT_FALSE(unbounded->GetByteSize().has_value());
}

// A complex type's byte size is twice its element's.
TEST_F(ContextTest, ComplexByteSizeIsDoubleElement) {
  BuiltinType *elem = context.GetBuiltinType(
      "float", 4, lldb::eEncodingIEEE754, lldb::eFormatFloat);
  ComplexType *complex = context.CreateComplexType(TypeRef(context, elem));
  EXPECT_EQ(complex->GetByteSize(), 8u);
}

// GetOrCreateDecl deduplicates by (kind, payload): the same payload always
// maps to the same Decl, but different payloads or kinds map to distinct ones.
TEST_F(ContextTest, DeclInterning) {
  int payload_a = 0;
  int payload_b = 0;
  const Decl *d1 =
      context.GetOrCreateDecl(Decl::Kind::MemberFunction, &payload_a);
  const Decl *d2 =
      context.GetOrCreateDecl(Decl::Kind::MemberFunction, &payload_a);
  EXPECT_EQ(d1, d2);

  const Decl *d3 =
      context.GetOrCreateDecl(Decl::Kind::MemberFunction, &payload_b);
  EXPECT_NE(d1, d3);

  const Decl *d4 =
      context.GetOrCreateDecl(Decl::Kind::StaticDataMember, &payload_a);
  EXPECT_NE(d1, d4);
}

// ForEachRecordType visits every RecordType owned by the Context (in creation
// order), but not non-record types.
TEST_F(ContextTest, ForEachRecordTypeVisitsRecordsOnly) {
  RecordType *r1 = context.CreateRecordType("First", 4, false);
  context.GetBuiltinType("int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  RecordType *r2 = context.CreateRecordType("Second", 8, true);

  std::vector<RecordType *> seen;
  context.ForEachRecordType(
      [&](RecordType *r) { seen.push_back(r); });

  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0], r1);
  EXPECT_EQ(seen[1], r2);
}
