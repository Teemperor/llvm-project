//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/Identifier.h"

#include "llvm/ADT/APInt.h"
#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

namespace {
struct TypeTest : public testing::Test {
  IdentifierMap ids;
};
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
  auto doNotComplete = [](Type *) { FAIL() << "nothing to complete"; };
  EXPECT_EQ(type.GetDynamicTypeCandidate(
                           DynamicTypeCheck::CPlusPlus | DynamicTypeCheck::ObjC,
                           doNotComplete), nullptr);
  EXPECT_FALSE(type.IsDynamicClass(doNotComplete));

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

  // Don't complete the type, but calling it is fine.
  auto complete = [](Type *) {};
  EXPECT_FALSE(type.HasFields(complete));
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

// Enumerator values that fit into 64 bits are stored inline, wider ones (such
// as the values of an `__int128` enum) as an APInt.
TEST_F(TypeTest, EnumeratorValues) {
  Enumerator narrow(ids.get("narrow"), uint64_t(42));
  EXPECT_FALSE(narrow.IsWide());
  EXPECT_EQ(narrow.GetValue(), 42u);
  EXPECT_EQ(narrow.GetAPInt(128, /*is_signed=*/false), llvm::APInt(128, 42));

  // A 64-bit negative value is sign-extended only for signed enumerations.
  Enumerator negative(ids.get("negative"), uint64_t(-1));
  EXPECT_TRUE(negative.GetAPInt(128, /*is_signed=*/true).isAllOnes());
  EXPECT_EQ(negative.GetAPInt(128, /*is_signed=*/false).getActiveBits(), 64u);

  // An APInt that fits into 64 bits is stored inline too.
  Enumerator small_apint(ids.get("small"), llvm::APInt(128, 7));
  EXPECT_FALSE(small_apint.IsWide());
  EXPECT_EQ(small_apint.GetValue(), 7u);

  // The same goes for a negative one, but not for a value that only fits into
  // 64 bits when it is unsigned.
  Enumerator small_negative(ids.get("small_negative"),
                            llvm::APInt(128, -5, /*isSigned=*/true));
  EXPECT_FALSE(small_negative.IsWide());
  EXPECT_EQ(small_negative.GetAPInt(128, /*is_signed=*/true),
            llvm::APInt(128, -5, /*isSigned=*/true));
  Enumerator top_bit(ids.get("top_bit"), llvm::APInt(128, 1).shl(63));
  EXPECT_TRUE(top_bit.IsWide());
  EXPECT_EQ(top_bit.GetAPInt(128, /*is_signed=*/true),
            llvm::APInt(128, 1).shl(63));

  llvm::APInt big_value = llvm::APInt(128, 1).shl(100);
  Enumerator wide(ids.get("wide"), big_value);
  EXPECT_TRUE(wide.IsWide());
  EXPECT_EQ(wide.GetWideValue(), big_value);
  EXPECT_EQ(wide.GetAPInt(128, /*is_signed=*/false), big_value);
  EXPECT_EQ(wide.GetName().GetName(), "wide");

  // Enumerators are copied and moved around inside of vectors.
  std::vector<Enumerator> enumerators = {narrow, wide};
  enumerators.push_back(negative);
  enumerators.push_back(Enumerator(ids.get("other"), big_value + 1));
  enumerators[0] = enumerators[3];
  EXPECT_TRUE(enumerators[0].IsWide());
  EXPECT_EQ(enumerators[0].GetWideValue(), big_value + 1);
  enumerators[3] = narrow;
  EXPECT_FALSE(enumerators[3].IsWide());
  EXPECT_EQ(enumerators[3].GetValue(), 42u);
  EXPECT_EQ(enumerators[1].GetWideValue(), big_value);
}
