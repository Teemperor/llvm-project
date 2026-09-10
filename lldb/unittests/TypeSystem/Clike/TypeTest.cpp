//===-- TypeTest.cpp ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/Identifier.h"

#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

namespace {
struct TypeTest : public testing::Test {
  IdentifierMap ids;

  /// An unnamed record of \p byte_size bytes. Fields can only be added
  /// through a Context, so tests here stay with what a bare record reports.
  static RecordType MakeRecord(std::optional<uint64_t> byte_size) {
    return RecordType(Identifier(), DeclScope(), byte_size);
  }
};
} // namespace

// A TypeRef always names a type and hands back that exact instance.
TEST_F(TypeTest, TypeRefNamesItsType) {
  RecordType record;
  TypeRef ref{record};
  EXPECT_EQ(&ref.Get(), &record);
}

// The base Type reports the "structural type" defaults: no name, no size, no
// declaration context, and not an aggregate.
TEST_F(TypeTest, BaseTypeDefaults) {
  Type type;
  EXPECT_TRUE(type.GetName().GetName().empty());
  EXPECT_TRUE(type.GetUnqualifiedName().GetName().empty());
  EXPECT_EQ(type.GetDeclContext(), nullptr);
  EXPECT_FALSE(type.GetByteSize().has_value());
  EXPECT_FALSE(type.GetAlignmentInBits().has_value());
  EXPECT_FALSE(type.IsAggregate());
  EXPECT_TRUE(type.IsComplete());
  EXPECT_FALSE(type.IsPolymorphic());
  EXPECT_EQ(type.GetNumFields(), 0u);
  EXPECT_EQ(type.GetFieldAtIndex(0), nullptr);
  EXPECT_EQ(type.GetNumBaseClasses(), 0u);
  EXPECT_EQ(type.GetBaseClassAtIndex(0), nullptr);
  EXPECT_EQ(type.GetTransparentChildPointee(), nullptr);
  EXPECT_EQ(type.GetNamedMemberPointee(), nullptr);
  EXPECT_EQ(type.GetEncoding(), lldb::eEncodingInvalid);
  EXPECT_EQ(type.GetTypeClass(), lldb::eTypeClassOther);
  EXPECT_EQ(type.GetMinimumLanguage(), lldb::eLanguageTypeC);
}

// Every kind-specific query answers "doesn't apply" on a kind that doesn't
// override it, so callers never need to know which kind they hold.
TEST_F(TypeTest, BaseTypeKindQueryDefaults) {
  Type type;
  EXPECT_EQ(type.StripElaborated(), &type);
  EXPECT_EQ(type.StripCVQualifiers(), &type);
  EXPECT_EQ(type.GetCVRQualifiers(), 0u);
  EXPECT_FALSE(type.IsConst());
  EXPECT_FALSE(type.GetPtrAuthQualifier().has_value());
  EXPECT_EQ(type.GetTypedefedType(), nullptr);

  EXPECT_FALSE(type.IsVoid());
  EXPECT_FALSE(type.IsChar());
  EXPECT_FALSE(type.IsInteger());
  EXPECT_FALSE(type.IsPromotableInteger());
  EXPECT_EQ(type.GetBasicTypeEnumeration(), lldb::eBasicTypeInvalid);

  EXPECT_FALSE(type.IsPointer());
  EXPECT_FALSE(type.IsBlockPointer());
  EXPECT_FALSE(type.IsFunctionPointer());
  EXPECT_FALSE(type.IsReference());
  EXPECT_FALSE(type.IsRValueReference());
  EXPECT_EQ(type.GetPointeeType(), nullptr);
  EXPECT_FALSE(type.IsMemberFunctionPointer());
  EXPECT_FALSE(type.IsMemberDataPointer());
  auto complete = [](Type *) { FAIL() << "nothing to complete"; };
  EXPECT_EQ(type.GetDynamicTypeCandidate(true, true, complete), nullptr);
  EXPECT_FALSE(type.IsDynamicClass(complete));

  EXPECT_EQ(type.GetArrayElementType(), nullptr);
  EXPECT_FALSE(type.GetNumElements().has_value());
  EXPECT_FALSE(type.IsVector());
  EXPECT_EQ(type.GetDIEUID(), UINT64_MAX);

  EXPECT_FALSE(type.IsFunction());
  EXPECT_EQ(type.GetReturnType(), nullptr);
  EXPECT_EQ(type.GetNumParameters(), 0u);
  EXPECT_EQ(type.GetParameterAtIndex(0), nullptr);

  EXPECT_EQ(type.GetEnumerationIntegerType(), nullptr);
  EXPECT_FALSE(type.IsScopedEnumeration());
  EXPECT_TRUE(type.GetEnumerators().empty());

  EXPECT_FALSE(type.IsRecord());
  EXPECT_FALSE(type.IsObjCInterface());
  EXPECT_FALSE(type.IsUnion());
  EXPECT_FALSE(type.IsClassKeyword());
  EXPECT_FALSE(type.IsAnonymousStructOrUnion());
  EXPECT_FALSE(type.IsTemplateInstantiation());
  EXPECT_EQ(type.GetNumTemplateArguments(), 0u);
  // Nothing to parse, so already "parsed".
  EXPECT_TRUE(type.AreMemberFunctionsParsed());
  EXPECT_EQ(type.GetNumMemberFunctions(), 0u);
  EXPECT_EQ(type.GetNumObjCMethods(), 0u);
  EXPECT_EQ(type.GetObjCMethodAtIndex(0), nullptr);
  EXPECT_EQ(type.GetNumStaticDataMembers(), 0u);
  EXPECT_EQ(type.GetNestedTypeWithName("anything"), nullptr);
  EXPECT_EQ(type.GetNumNestedTypes(), 0u);
  uint32_t num_fields = 1;
  EXPECT_EQ(type.GetHomogeneousAggregateBase(num_fields), nullptr);
  EXPECT_EQ(num_fields, 0u);
}

// The NamedType mixin supplies the name/decl-context storage that Type only
// declares as virtual accessors.
TEST_F(TypeTest, NamedTypeStoresNames) {
  RecordType record(ids.get("std::vector<int, std::allocator<int> >"),
                    DeclScope{/*decl_context=*/nullptr, ids.get("vector<int>")});
  EXPECT_EQ(record.GetName().GetName(),
            "std::vector<int, std::allocator<int> >");
  EXPECT_EQ(record.GetUnqualifiedName().GetName(), "vector<int>");
  EXPECT_EQ(record.GetDeclContext(), nullptr);
}

// The ByteSizedType mixin round-trips a size, and models "unknown" distinctly
// from zero (its sentinel is UINT64_MAX, not 0).
TEST_F(TypeTest, ByteSizedTypeStoresSize) {
  EXPECT_FALSE(RecordType().GetByteSize().has_value());
  EXPECT_EQ(MakeRecord(8).GetByteSize(), std::optional<uint64_t>(8));
  // Zero is a real size (an empty struct), not "unknown".
  EXPECT_EQ(MakeRecord(0).GetByteSize(), std::optional<uint64_t>(0));
  EXPECT_FALSE(MakeRecord(std::nullopt).GetByteSize().has_value());
}

// Without a recorded alignment, the alignment is the largest power of two that
// divides the size, capped at 8 bytes.
TEST_F(TypeTest, AlignmentDerivedFromSize) {
  EXPECT_EQ(MakeRecord(1).GetAlignmentInBits(), std::optional<uint64_t>(8));
  EXPECT_EQ(MakeRecord(2).GetAlignmentInBits(), std::optional<uint64_t>(16));
  EXPECT_EQ(MakeRecord(4).GetAlignmentInBits(), std::optional<uint64_t>(32));
  EXPECT_EQ(MakeRecord(8).GetAlignmentInBits(), std::optional<uint64_t>(64));
  // Capped at the fundamental alignment even though 16 divides 16.
  EXPECT_EQ(MakeRecord(16).GetAlignmentInBits(), std::optional<uint64_t>(64));
  // 12 is 4-aligned: 8 does not divide it.
  EXPECT_EQ(MakeRecord(12).GetAlignmentInBits(), std::optional<uint64_t>(32));
  // An odd size is byte-aligned.
  EXPECT_EQ(MakeRecord(3).GetAlignmentInBits(), std::optional<uint64_t>(8));
}

// Neither a recorded alignment nor a usable size means "no alignment" rather
// than a made-up one.
TEST_F(TypeTest, AlignmentUnknownWithoutSize) {
  EXPECT_FALSE(MakeRecord(std::nullopt).GetAlignmentInBits().has_value());
  // A zero size carries no alignment information either.
  EXPECT_FALSE(MakeRecord(0).GetAlignmentInBits().has_value());
}

// A record is an aggregate and starts out as a forward declaration.
TEST_F(TypeTest, RecordDefaults) {
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

// A Field describes a plain member unless it carries a bitfield width.
TEST_F(TypeTest, FieldBitfield) {
  RecordType int_type;
  Field plain{ids.get("x"), TypeRef(int_type), /*byte_offset=*/4};
  EXPECT_FALSE(plain.IsBitfield());
  EXPECT_EQ(plain.byte_offset, 4u);
  EXPECT_EQ(plain.bitfield_bit_size, 0u);

  Field bits{ids.get("b"), TypeRef(int_type), /*byte_offset=*/0,
             /*bitfield_bit_size=*/3, /*bitfield_bit_offset=*/5};
  EXPECT_TRUE(bits.IsBitfield());
  EXPECT_EQ(bits.bitfield_bit_size, 3u);
  EXPECT_EQ(bits.bitfield_bit_offset, 5u);
}

// A non-virtual base records a constant offset; a virtual one leaves
// byte_offset at 0 and (when DWARF spelled the standard expression) reports a
// vbase_offset_offset instead.
TEST_F(TypeTest, BaseClassVirtualOffset) {
  RecordType base_record;
  BaseClass direct{TypeRef(base_record), /*byte_offset=*/16};
  EXPECT_FALSE(direct.is_virtual);
  EXPECT_EQ(direct.byte_offset, 16u);
  EXPECT_FALSE(direct.vbase_offset_offset.has_value());

  BaseClass virt{TypeRef(base_record), /*byte_offset=*/0, /*is_virtual=*/true,
                 /*vbase_offset_offset=*/24};
  EXPECT_TRUE(virt.is_virtual);
  EXPECT_EQ(virt.byte_offset, 0u);
  EXPECT_EQ(virt.vbase_offset_offset, std::optional<uint64_t>(24));
}

// Sugar forwards the layout/value queries to what it wraps, so a wrapped
// aggregate still looks like one.
TEST_F(TypeTest, SugarForwardsToUnderlyingType) {
  RecordType record = MakeRecord(24);
  SugarType sugar{TypeRef(record)};

  EXPECT_EQ(sugar.GetUnderlyingType(), &record);
  EXPECT_TRUE(sugar.IsAggregate());
  EXPECT_EQ(sugar.GetByteSize(), std::optional<uint64_t>(24));
  EXPECT_EQ(sugar.GetTypeInfo(), record.GetTypeInfo());
  EXPECT_EQ(sugar.GetNumFields(), record.GetNumFields());
  EXPECT_EQ(sugar.GetNumBaseClasses(), record.GetNumBaseClasses());
}

// Desugaring peels every layer of sugar in one step, reaching the canonical
// type; a canonical type desugars to itself.
TEST_F(TypeTest, DesugarReachesCanonicalType) {
  RecordType record;
  SugarType one{TypeRef(record)};
  SugarType two{TypeRef(one)};
  SugarType three{TypeRef(two)};

  EXPECT_EQ(record.Desugar(), &record);
  EXPECT_EQ(one.Desugar(), &record);
  EXPECT_EQ(two.Desugar(), &record);
  EXPECT_EQ(three.Desugar(), &record);
  // Only one layer is peeled by GetUnderlyingType.
  EXPECT_EQ(three.GetUnderlyingType(), &two);
}

// The free Desugar() tolerates the null a failed lookup hands it.
TEST_F(TypeTest, FreeDesugarIsNullTolerant) {
  RecordType record;
  SugarType sugar{TypeRef(record)};
  EXPECT_EQ(Desugar(static_cast<Type *>(nullptr)), nullptr);
  EXPECT_EQ(Desugar(static_cast<const Type *>(nullptr)), nullptr);
  EXPECT_EQ(Desugar(&sugar), &record);

  const SugarType &const_sugar = sugar;
  EXPECT_EQ(Desugar(&const_sugar), &record);
}

// LLVM-style RTTI distinguishes the core kinds, including through a reference
// held as the base Type.
TEST_F(TypeTest, RTTI) {
  RecordType record;
  SugarType sugar{TypeRef(record)};

  Type *as_type = &record;
  EXPECT_TRUE(llvm::isa<RecordType>(as_type));
  EXPECT_FALSE(llvm::isa<SugarType>(as_type));
  EXPECT_EQ(llvm::dyn_cast<RecordType>(as_type), &record);
  EXPECT_EQ(llvm::dyn_cast<SugarType>(as_type), nullptr);

  as_type = &sugar;
  EXPECT_TRUE(llvm::isa<SugarType>(as_type));
  EXPECT_FALSE(llvm::isa<RecordType>(as_type));

  // ...and tolerates a null through the _or_null forms.
  Type *null_type = nullptr;
  EXPECT_EQ(llvm::dyn_cast_or_null<RecordType>(null_type), nullptr);
}

// HasFields sees through sugar and completes each record it inspects. A
// record with no definition anywhere reports "has fields" so that callers can
// surface it as incomplete rather than silently hiding it.
TEST_F(TypeTest, HasFields) {
  RecordType incomplete;
  SugarType sugar{TypeRef(incomplete)};

  std::vector<Type *> completed;
  auto complete = [&completed](Type *t) { completed.push_back(t); };

  EXPECT_TRUE(sugar.HasFields(complete));
  // Desugared before being completed and inspected.
  ASSERT_EQ(completed.size(), 1u);
  EXPECT_EQ(completed[0], &incomplete);

  // A non-record has no fields.
  Type plain;
  completed.clear();
  EXPECT_FALSE(plain.HasFields(complete));
}
