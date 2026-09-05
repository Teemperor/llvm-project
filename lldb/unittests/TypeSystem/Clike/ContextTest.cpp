//===-- ContextTest.cpp -------------------------------------------------===//
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
#include "Plugins/TypeSystem/Clike/TypeCpp.h"

#include "llvm/Support/Casting.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

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
  PointerType *p1 = context.CreatePointerType(TypeRef(*record));
  PointerType *p2 = context.CreatePointerType(TypeRef(*record));
  EXPECT_EQ(p1, p2);
  EXPECT_EQ(p1->GetPointeeType(), record);
  EXPECT_EQ(p1->GetByteSize(), 8u);
}

// A block pointer and a plain pointer to the same pointee are distinct types
// (the block pointer is a separate BlockPointerType kind).
TEST_F(ContextTest, BlockPointerDistinctFromPlainPointer) {
  Type *record = context.CreateRecordType("Foo", 4, false);
  PointerType *plain = context.CreatePointerType(TypeRef(*record));
  BlockPointerType *block =
      context.CreateBlockPointerType(TypeRef(*record));
  EXPECT_NE(static_cast<Type *>(plain), static_cast<Type *>(block));
  EXPECT_TRUE(llvm::isa<BlockPointerType>(block));
  EXPECT_FALSE(llvm::isa<BlockPointerType>(plain));
  // A BlockPointerType is still a PointerType.
  EXPECT_TRUE(llvm::isa<PointerType>(block));
}

// `void *` is a pointer to the `void` builtin, not a pointer to nothing: a
// TypeRef always names a type.
TEST_F(ContextTest, VoidPointer) {
  BuiltinType *void_type = context.GetBuiltinType(BuiltinKind::Void);
  PointerType *p = context.CreatePointerType(TypeRef(*void_type));
  EXPECT_EQ(p->GetPointeeType(), void_type);
  EXPECT_TRUE(IsVoid(p->GetPointeeType()));
  EXPECT_EQ(p->GetByteSize(), 8u);
}

// A typedef inherits the byte size of the type it aliases.
TEST_F(ContextTest, TypedefInheritsByteSize) {
  Type *record = context.CreateRecordType("Foo", 16, false);
  TypedefType *td =
      context.CreateTypedefType("FooAlias", TypeRef(*record));
  EXPECT_EQ(td->GetByteSize(), 16u);
  EXPECT_EQ(td->GetUnderlyingType(), record);
}

// An array's byte size is element size times element count, when both are
// known; an array of unknown bound has no byte size of its own.
TEST_F(ContextTest, ArrayByteSizeComputed) {
  BuiltinType *elem =
      context.GetBuiltinType("int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  ArrayType *bounded =
      context.CreateArrayType(TypeRef(*elem), /*num_elements=*/10);
  EXPECT_EQ(bounded->GetByteSize(), 40u);

  ArrayType *unbounded =
      context.CreateArrayType(TypeRef(*elem), std::nullopt);
  EXPECT_FALSE(unbounded->GetByteSize().has_value());
}

// A complex type's byte size is twice its element's.
TEST_F(ContextTest, ComplexByteSizeIsDoubleElement) {
  BuiltinType *elem = context.GetBuiltinType(
      "float", 4, lldb::eEncodingIEEE754, lldb::eFormatFloat);
  ComplexType *complex = context.CreateComplexType(TypeRef(*elem));
  EXPECT_EQ(complex->GetByteSize(), 8u);
}

// Every type records the Context that created it, including the canonical
// builtins (which KnownBuiltinTypes, not Track, creates).
TEST_F(ContextTest, TypesKnowTheirOwningContext) {
  Type *record = context.CreateRecordType("Foo", 4, /*is_cpp_class=*/false);
  EXPECT_EQ(&record->GetOwningContext(), &context);
  Type *pointer = context.CreatePointerType(TypeRef(*record));
  EXPECT_EQ(&pointer->GetOwningContext(), &context);
  Type *builtin = context.GetBuiltinType(BuiltinKind::Int);
  EXPECT_EQ(&builtin->GetOwningContext(), &context);
}

// A type may reference one another Context owns directly: the referenced node
// records its own owner, so nothing has to be interposed to keep that
// recoverable, and type identity stays node identity across Contexts.
TEST_F(ContextTest, ReferenceToAnotherContextsTypeIsDirect) {
  LanguageOpts other_opts{llvm::Triple("x86_64-pc-linux-gnu")};
  Context other{other_opts};
  Type *foreign_record = other.CreateRecordType("Foo", 4,
                                                /*is_cpp_class=*/false);
  EXPECT_EQ(&foreign_record->GetOwningContext(), &other);

  // A pointer created here points straight at it -- no wrapper node, and the
  // pointer is still ours (it takes its width from this Context).
  PointerType *ptr = context.CreatePointerType(TypeRef(*foreign_record));
  EXPECT_EQ(&ptr->GetOwningContext(), &context);
  EXPECT_EQ(ptr->GetPointeeType(), foreign_record);
  EXPECT_EQ(&ptr->GetPointeeType()->GetOwningContext(), &other);

  // The pointee needs no unwrapping to be recognized for what it is.
  EXPECT_TRUE(llvm::isa<RecordType>(ptr->GetPointeeType()));
  EXPECT_EQ(ptr->GetPointeeType()->GetName().GetName(), "Foo");
  EXPECT_EQ(ptr->GetPointeeType()->GetByteSize(), 4u);

  // Pointer uniquing keys on the referenced node, so two `Foo *` formed here
  // are still the same instance.
  EXPECT_EQ(context.CreatePointerType(TypeRef(*foreign_record)), ptr);
}

// GetOrCreateDecl deduplicates by payload: the same payload pointer always
// maps to the same Decl, and a different payload -- whether a different
// object of the same alternative (StaticDataMember/MemberFunction) or an
// object of the other alternative -- maps to a distinct one.
TEST_F(ContextTest, DeclInterning) {
  BuiltinType *void_type = context.GetBuiltinType(BuiltinKind::Void);
  FunctionType *fn = context.CreateFunctionType(TypeRef(*void_type),
                                                /*is_variadic=*/false);
  MemberFunction payload_a{TypeRef(*fn)};
  MemberFunction payload_b{TypeRef(*fn)};
  const Decl *d1 = context.GetOrCreateDecl(&payload_a);
  const Decl *d2 = context.GetOrCreateDecl(&payload_a);
  EXPECT_EQ(d1, d2);

  const Decl *d3 = context.GetOrCreateDecl(&payload_b);
  EXPECT_NE(d1, d3);

  StaticDataMember payload_c{Identifier(), TypeRef(*void_type), Identifier(),
                             std::nullopt};
  const Decl *d4 = context.GetOrCreateDecl(&payload_c);
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
