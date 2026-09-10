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
};

/// A type of a given size, for testing the size-derived defaults of Type.
struct SizedType : public Type {
  explicit SizedType(std::optional<uint64_t> byte_size)
      : m_byte_size(byte_size) {}
  std::optional<uint64_t> GetByteSize() const override { return m_byte_size; }
  std::optional<uint64_t> m_byte_size;
};

SizedType MakeSized(std::optional<uint64_t> byte_size) {
  return SizedType(byte_size);
}
} // namespace

// The Type class interface should return by default what most subclasses
// return.
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

// Without a recorded alignment, the alignment is the largest power of two that
// divides the size, capped at 8 bytes.
TEST_F(TypeTest, AlignmentDerivedFromSize) {
  EXPECT_EQ(MakeSized(1).GetAlignmentInBits(), std::optional<uint64_t>(8));
  EXPECT_EQ(MakeSized(2).GetAlignmentInBits(), std::optional<uint64_t>(16));
  EXPECT_EQ(MakeSized(4).GetAlignmentInBits(), std::optional<uint64_t>(32));
  EXPECT_EQ(MakeSized(8).GetAlignmentInBits(), std::optional<uint64_t>(64));
  // Capped at the fundamental alignment even though 16 divides 16.
  EXPECT_EQ(MakeSized(16).GetAlignmentInBits(), std::optional<uint64_t>(64));
  // 12 is 4-aligned: 8 does not divide it.
  EXPECT_EQ(MakeSized(12).GetAlignmentInBits(), std::optional<uint64_t>(32));
  // An odd size is byte-aligned.
  EXPECT_EQ(MakeSized(3).GetAlignmentInBits(), std::optional<uint64_t>(8));
}

// Neither a recorded alignment nor a usable size means "no alignment" rather
// than a made-up one.
TEST_F(TypeTest, AlignmentUnknownWithoutSize) {
  EXPECT_FALSE(MakeSized(std::nullopt).GetAlignmentInBits().has_value());
  // A zero size carries no alignment information either.
  EXPECT_FALSE(MakeSized(0).GetAlignmentInBits().has_value());
}

TEST_F(TypeTest, FieldBitfield) {
  Type int_type;
  Field plain{ids.get("x"), TypeRef(int_type), /*byte_offset=*/4};
  EXPECT_FALSE(plain.IsBitfield());
  EXPECT_EQ(plain.GetByteOffset(), 4u);

  Field bits{ids.get("b"), TypeRef(int_type), /*byte_offset=*/0,
             /*bitfield_bit_size=*/3, /*bitfield_bit_offset=*/5};
  EXPECT_TRUE(bits.IsBitfield());
  EXPECT_EQ(bits.GetBitfieldBitSize(), 3u);
  EXPECT_EQ(bits.GetBitfieldBitOffset(), 5u);
}

// A non-virtual base records the offset we get from debug information. A
// virtual class doesn't report a fixed offset but returns vbase_offset_offset
// instead.
TEST_F(TypeTest, BaseClassVirtualOffset) {
  Type base_record;
  BaseClass direct{TypeRef(base_record), /*byte_offset=*/16};
  EXPECT_FALSE(direct.IsVirtual());
  EXPECT_EQ(direct.GetByteOffset(), 16u);
  EXPECT_FALSE(direct.GetVBaseOffsetOffset().has_value());

  BaseClass virt{TypeRef(base_record), /*byte_offset=*/0, /*is_virtual=*/true,
                 /*vbase_offset_offset=*/24};
  EXPECT_TRUE(virt.IsVirtual());
  EXPECT_EQ(virt.GetByteOffset(), 0u);
  EXPECT_EQ(virt.GetVBaseOffsetOffset(), std::optional<uint64_t>(24));
}

// A non-record has no fields.
TEST_F(TypeTest, HasFields) {
  Type plain;
  auto complete = [](Type *) {};
  EXPECT_FALSE(plain.HasFields(complete));
}
