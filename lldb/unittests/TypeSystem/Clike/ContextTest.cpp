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

// A record is created as a StructType and starts out incomplete.
TEST_F(ContextTest, CreateStructRecord) {
  RecordType *r = context.CreateRecordType("Foo", /*byte_size=*/4);
  ASSERT_NE(r, nullptr);
  EXPECT_TRUE(llvm::isa<StructType>(r));
  EXPECT_EQ(r->GetName().GetName(), "Foo");
  EXPECT_EQ(r->GetByteSize(), 4u);
  EXPECT_FALSE(r->IsComplete());
  EXPECT_FALSE(r->IsUnion());
}

// The is_union flag is threaded through to the created record.
TEST_F(ContextTest, CreateUnionRecord) {
  RecordType *r = context.CreateRecordType("U", /*byte_size=*/4,
                                           /*is_union=*/true);
  EXPECT_TRUE(r->IsUnion());
}

// The declaration scope a named type is created with is what it reports.
TEST_F(ContextTest, CreateRecordWithDeclScope) {
  const Namespace *ns =
      context.GetNamespace(context.GetIdentifier("std"), nullptr, false);
  RecordType *r =
      context.CreateRecordType("std::string", /*byte_size=*/32,
                               /*is_union=*/false, /*is_class_keyword=*/true,
                               DeclScope{ns, context.GetIdentifier("string")});
  EXPECT_EQ(r->GetName().GetName(), "std::string");
  EXPECT_EQ(r->GetDeclContext(), ns);
  EXPECT_EQ(r->GetUnqualifiedName().GetName(), "string");
  EXPECT_TRUE(r->IsClassKeyword());
}

// A record created from a forward declaration learns its size (and any
// explicitly recorded alignment) when it is completed. Sugar over it forwards
// the size dynamically, so it picks up the size learned later.
TEST_F(ContextTest, RecordLearnsSizeOnCompletion) {
  RecordType *r = context.CreateRecordType("Fwd", /*byte_size=*/std::nullopt);
  TypedefType *td = context.CreateTypedefType("FwdAlias", TypeRef(*r));
  EXPECT_FALSE(td->GetByteSize().has_value());
  context.SetByteSize(*r, 48);
  EXPECT_EQ(r->GetByteSize(), 48u);
  EXPECT_EQ(td->GetByteSize(), 48u);
}

// An explicitly-recorded alignment (DW_AT_alignment) wins over the alignment
// of the members.
TEST_F(ContextTest, ExplicitAlignmentWins) {
  RecordType *r = context.CreateRecordType("Aligned", /*byte_size=*/8);
  context.SetExplicitAlignmentInBits(*r, 1024);
  EXPECT_EQ(r->GetExplicitAlignmentInBits(), std::optional<uint64_t>(1024));
  EXPECT_EQ(r->GetAlignmentInBits(), std::optional<uint64_t>(1024));
}

// An explicitly-recorded zero alignment is ignored, not reported.
TEST_F(ContextTest, ExplicitZeroAlignmentIgnored) {
  RecordType *r =
      context.CreateRecordType("Unaligned", /*byte_size=*/std::nullopt);
  context.SetExplicitAlignmentInBits(*r, 0);
  EXPECT_FALSE(r->GetAlignmentInBits().has_value());
}

// Without an explicit alignment, a record is aligned like its most aligned
// member. Unnamed bitfields and members with an unknown alignment don't
// contribute. An incomplete record has no known alignment.
TEST_F(ContextTest, RecordAlignmentFromMembers) {
  Type *c = context.GetBuiltinType(BuiltinKind::Char);
  Type *i = context.GetBuiltinType(BuiltinKind::Int);
  Type *ld = context.GetBuiltinType(BuiltinKind::LongDouble);

  RecordType *r = context.CreateRecordType("R", /*byte_size=*/32);
  EXPECT_FALSE(r->GetAlignmentInBits().has_value());
  context.AddField(*r, context.GetIdentifier("c"), TypeRef(*c), 0);
  context.AddField(*r, context.GetIdentifier("ld"), TypeRef(*ld), 16);
  context.SetComplete(*r);
  EXPECT_EQ(r->GetAlignmentInBits(), 128u);

  RecordType *bits = context.CreateRecordType("Bits", /*byte_size=*/2);
  context.AddField(*bits, context.GetIdentifier("c"), TypeRef(*c), 0);
  context.AddField(*bits, context.GetIdentifier(""), TypeRef(*i), 0,
                   /*bitfield_bit_size=*/3, /*bitfield_bit_offset=*/8);
  BuiltinType *unknown = context.GetBuiltinType(
      "unknown", 2, lldb::eEncodingVector, lldb::eFormatHex);
  context.AddField(*bits, context.GetIdentifier("u"), TypeRef(*unknown), 0);
  context.SetComplete(*bits);
  EXPECT_EQ(bits->GetAlignmentInBits(), 8u);

  RecordType *empty = context.CreateRecordType("Empty", /*byte_size=*/1);
  context.SetComplete(*empty);
  EXPECT_EQ(empty->GetAlignmentInBits(), 8u);
}

// A record whose members aren't at a multiple of their alignment, or whose
// size isn't a multiple of its members' alignment, is packed.
TEST_F(ContextTest, PackedRecordAlignment) {
  Type *c = context.GetBuiltinType(BuiltinKind::Char);
  Type *i = context.GetBuiltinType(BuiltinKind::Int);

  RecordType *misaligned = context.CreateRecordType("M", /*byte_size=*/5);
  context.AddField(*misaligned, context.GetIdentifier("c"), TypeRef(*c), 0);
  context.AddField(*misaligned, context.GetIdentifier("i"), TypeRef(*i), 1);
  context.SetComplete(*misaligned);
  EXPECT_EQ(misaligned->GetAlignmentInBits(), 8u);

  RecordType *short_size = context.CreateRecordType("S", /*byte_size=*/5);
  context.AddField(*short_size, context.GetIdentifier("i"), TypeRef(*i), 0);
  context.AddField(*short_size, context.GetIdentifier("c"), TypeRef(*c), 4);
  context.SetComplete(*short_size);
  EXPECT_EQ(short_size->GetAlignmentInBits(), 8u);
}

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

// Pointer types are uniqued by (pointee, is_block): forming the same pointer
// twice returns the identical instance.
TEST_F(ContextTest, PointerTypesAreUniqued) {
  Type *record = context.CreateRecordType("Foo", 4);
  PointerType *p1 = context.CreatePointerType(TypeRef(*record));
  PointerType *p2 = context.CreatePointerType(TypeRef(*record));
  EXPECT_EQ(p1, p2);
  EXPECT_EQ(p1->GetPointeeType(), record);
  EXPECT_EQ(p1->GetByteSize(), 8u);
}

// A block pointer and a plain pointer to the same pointee are distinct types
// (the block pointer is a separate BlockPointerType kind).
TEST_F(ContextTest, BlockPointerDistinctFromPlainPointer) {
  Type *record = context.CreateRecordType("Foo", 4);
  PointerType *plain = context.CreatePointerType(TypeRef(*record));
  BlockPointerType *block = context.CreateBlockPointerType(TypeRef(*record));
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
  EXPECT_TRUE(p->GetPointeeType()->IsVoid());
  EXPECT_EQ(p->GetByteSize(), 8u);
}

// A typedef inherits the byte size of the type it aliases.
TEST_F(ContextTest, TypedefInheritsByteSize) {
  Type *record = context.CreateRecordType("Foo", 16);
  TypedefType *td = context.CreateTypedefType("FooAlias", TypeRef(*record));
  EXPECT_EQ(td->GetByteSize(), 16u);
  EXPECT_EQ(td->GetUnderlyingType(), record);
}

// An array's byte size is element size times element count, when both are
// known; an array of unknown bound has no byte size of its own.
TEST_F(ContextTest, ArrayByteSizeComputed) {
  BuiltinType *elem = context.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                             lldb::eFormatDecimal);
  ArrayType *bounded =
      context.CreateArrayType(TypeRef(*elem), /*num_elements=*/10);
  EXPECT_EQ(bounded->GetByteSize(), 40u);

  ArrayType *unbounded = context.CreateArrayType(TypeRef(*elem), std::nullopt);
  EXPECT_FALSE(unbounded->GetByteSize().has_value());
}

// A complex type's byte size is twice its element's.
TEST_F(ContextTest, ComplexByteSizeIsDoubleElement) {
  BuiltinType *elem = context.GetBuiltinType("float", 4, lldb::eEncodingIEEE754,
                                             lldb::eFormatFloat);
  ComplexType *complex = context.CreateComplexType(TypeRef(*elem));
  EXPECT_EQ(complex->GetByteSize(), 8u);
}

// Every type records the Context that created it, including the canonical
// builtins (which KnownBuiltinTypes, not Track, creates).
TEST_F(ContextTest, TypesKnowTheirOwningContext) {
  Type *record = context.CreateRecordType("Foo", 4);
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
  Context other{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};
  Type *foreign_record = other.CreateRecordType("Foo", 4);
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

// ForEachRecordType visits every RecordType owned by the Context (in creation
// order), but not non-record types.
TEST_F(ContextTest, ForEachRecordTypeVisitsRecordsOnly) {
  RecordType *r1 = context.CreateRecordType("First", 4);
  context.GetBuiltinType("int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  RecordType *r2 = context.CreateRecordType("Second", 8);

  std::vector<RecordType *> seen;
  context.ForEachRecordType([&](RecordType *r) { seen.push_back(r); });

  ASSERT_EQ(seen.size(), 2u);
  EXPECT_EQ(seen[0], r1);
  EXPECT_EQ(seen[1], r2);
}
