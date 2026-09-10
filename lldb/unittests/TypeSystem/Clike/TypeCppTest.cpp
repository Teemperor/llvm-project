//===-- TypeCppTest.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/TypeCpp.h"
#include "Plugins/TypeSystem/Clike/Identifier.h"
#include "Plugins/TypeSystem/Clike/Type.h"

#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

namespace {
struct TypeCppTest : public testing::Test {
  IdentifierMap ids;
};
} // namespace

// A C++ class reports the class type class and, unlike a plain C struct, can
// carry base classes and the C++-only member lists.
TEST_F(TypeCppTest, ClassDefaults) {
  ClassType record;
  EXPECT_EQ(record.GetTypeClass(), lldb::eTypeClassClass);
  EXPECT_TRUE(record.IsAggregate());
  EXPECT_FALSE(record.IsPolymorphic());
  EXPECT_FALSE(record.IsTemplateInstantiation());
  EXPECT_EQ(record.GetNumBaseClasses(), 0u);
  EXPECT_EQ(record.GetBaseClassAtIndex(0), nullptr);
  EXPECT_EQ(record.GetNumTemplateArguments(), 0u);
  EXPECT_EQ(record.GetTemplateArgumentAtIndex(0), nullptr);
  EXPECT_EQ(record.GetNumMemberFunctions(), 0u);
  EXPECT_EQ(record.GetMemberFunctionAtIndex(0), nullptr);
  EXPECT_EQ(record.GetNumStaticDataMembers(), 0u);
  EXPECT_EQ(record.GetStaticDataMemberAtIndex(0), nullptr);
  // A class is still a record, so RTTI sees it as both.
  Type *as_type = &record;
  EXPECT_TRUE(llvm::isa<RecordType>(as_type));
  EXPECT_TRUE(llvm::isa<ClassType>(as_type));
}

// A plain RecordType is not a ClassType: the C++-only storage exists only on
// the latter.
TEST_F(TypeCppTest, PlainRecordIsNotAClass) {
  RecordType plain;
  Type *as_type = &plain;
  EXPECT_FALSE(llvm::isa<ClassType>(as_type));
  EXPECT_EQ(plain.GetTypeClass(), lldb::eTypeClassOther);
}

// An lvalue reference reports the reference type class and its referent; the
// rvalue-ness is a separate flag rather than a distinct kind.
TEST_F(TypeCppTest, Reference) {
  RecordType referent;
  ReferenceType ref{TypeRef(referent)};

  EXPECT_EQ(ref.GetPointeeType(), &referent);
  EXPECT_FALSE(ref.IsRValue());
  EXPECT_EQ(ref.GetTypeClass(), lldb::eTypeClassReference);
  EXPECT_EQ(ref.GetEncoding(), lldb::eEncodingUint);
  EXPECT_EQ(ref.GetFormat(), lldb::eFormatHex);
  EXPECT_EQ(ref.GetTypeInfo(),
            uint32_t(lldb::eTypeHasChildren | lldb::eTypeIsReference |
                     lldb::eTypeHasValue));
  // A reference is not an aggregate itself, even when its referent is one.
  EXPECT_FALSE(ref.IsAggregate());

  ref.SetIsRValue(true);
  EXPECT_TRUE(ref.IsRValue());
}

// A reference stores the target's pointer width, and reports "unknown" until
// one is set (its compact storage uses 0 as the unset sentinel).
TEST_F(TypeCppTest, ReferenceByteSizeAndAlignment) {
  RecordType referent;
  ReferenceType ref{TypeRef(referent)};
  EXPECT_FALSE(ref.GetByteSize().has_value());
  EXPECT_FALSE(ref.GetAlignmentInBits().has_value());

  ref.SetByteSize(8);
  EXPECT_EQ(ref.GetByteSize(), std::optional<uint64_t>(8));
  // Pointer-aligned: the alignment is its own size, not the
  // largest-power-of-two-dividing-the-size heuristic Type applies.
  EXPECT_EQ(ref.GetAlignmentInBits(), std::optional<uint64_t>(64));

  ref.SetByteSize(4);
  EXPECT_EQ(ref.GetAlignmentInBits(), std::optional<uint64_t>(32));

  ref.SetByteSize(std::nullopt);
  EXPECT_FALSE(ref.GetByteSize().has_value());
}

// A reference to an incomplete aggregate keeps its single deref child (no
// splicing), and looking a name up through it does not require completeness.
TEST_F(TypeCppTest, ReferenceTransparencyRequiresCompleteness) {
  RecordType incomplete;
  ASSERT_FALSE(incomplete.IsComplete());
  ReferenceType ref{TypeRef(incomplete)};

  EXPECT_EQ(ref.GetTransparentChildPointee(), nullptr);
  // The by-name counterpart is deliberately more permissive.
  EXPECT_EQ(ref.GetNamedMemberPointee(), &incomplete);

  // A non-aggregate referent qualifies for neither.
  Type scalar;
  ReferenceType scalar_ref{TypeRef(scalar)};
  EXPECT_EQ(scalar_ref.GetTransparentChildPointee(), nullptr);
  EXPECT_EQ(scalar_ref.GetNamedMemberPointee(), nullptr);
}

// A pointer-to-member is an opaque scalar: it has no children and is not
// transparent, unlike a pointer or a reference.
TEST_F(TypeCppTest, MemberPointer) {
  RecordType containing;
  RecordType member_type;
  MemberPointerType mem_ptr{TypeRef(member_type), TypeRef(containing)};

  EXPECT_EQ(mem_ptr.GetPointeeType(), &member_type);
  EXPECT_EQ(mem_ptr.GetContainingType(), &containing);
  EXPECT_EQ(mem_ptr.GetTypeClass(), lldb::eTypeClassMemberPointer);
  EXPECT_EQ(mem_ptr.GetEncoding(), lldb::eEncodingUint);
  EXPECT_EQ(mem_ptr.GetFormat(), lldb::eFormatHex);
  EXPECT_EQ(mem_ptr.GetTypeInfo(),
            uint32_t(lldb::eTypeIsPointer | lldb::eTypeIsMember |
                     lldb::eTypeHasValue));
  EXPECT_EQ(mem_ptr.GetTransparentChildPointee(), nullptr);
  EXPECT_EQ(mem_ptr.GetNamedMemberPointee(), nullptr);
}

// The member-pointer width is ABI-defined and supplied by the DWARF parser
// rather than derived, so it round-trips through the compact storage.
TEST_F(TypeCppTest, MemberPointerByteSize) {
  RecordType containing;
  RecordType member_type;
  MemberPointerType mem_ptr{TypeRef(member_type), TypeRef(containing)};
  EXPECT_FALSE(mem_ptr.GetByteSize().has_value());

  // Itanium: sizeof(ptrdiff_t) for a data member...
  mem_ptr.SetByteSize(8);
  EXPECT_EQ(mem_ptr.GetByteSize(), std::optional<uint64_t>(8));
  // ...two pointers for a member function.
  mem_ptr.SetByteSize(16);
  EXPECT_EQ(mem_ptr.GetByteSize(), std::optional<uint64_t>(16));
}

// A static data member occupies no storage in the object: it either has a
// mangled name to resolve an address through, or a compile-time constant.
TEST_F(TypeCppTest, StaticDataMember) {
  RecordType int_type;
  StaticDataMember with_storage{ids.get("s"), TypeRef(int_type),
                                ids.get("_ZN3Foo1sE")};
  EXPECT_FALSE(with_storage.HasConstValue());
  EXPECT_EQ(with_storage.mangled_name.GetName(), "_ZN3Foo1sE");

  StaticDataMember constant{ids.get("k"), TypeRef(int_type), Identifier(),
                            /*const_value=*/42};
  EXPECT_TRUE(constant.HasConstValue());
  EXPECT_EQ(constant.const_value, std::optional<uint64_t>(42));
  // A constant-only member has no storage, hence no mangled name.
  EXPECT_TRUE(constant.mangled_name.GetName().empty());
}

// A template argument is a type argument, an integral value, or a
// template-template argument that names a template and has no type at all.
TEST_F(TypeCppTest, TemplateArgument) {
  RecordType int_type;

  TemplateArgument type_arg;
  type_arg.kind = lldb::eTemplateArgumentKindType;
  type_arg.type = TypeRef(int_type);
  ASSERT_TRUE(type_arg.type.has_value());
  EXPECT_EQ(&type_arg.type->Get(), &int_type);
  EXPECT_FALSE(type_arg.is_default);

  TemplateArgument integral;
  integral.kind = lldb::eTemplateArgumentKindIntegral;
  integral.type = TypeRef(int_type);
  integral.integral_value = 7;
  EXPECT_EQ(integral.integral_value, 7u);

  // The one genuinely optional reference in the model.
  TemplateArgument tmpl_arg;
  tmpl_arg.kind = lldb::eTemplateArgumentKindTemplate;
  tmpl_arg.name = ids.get("T1");
  EXPECT_FALSE(tmpl_arg.type.has_value());
  EXPECT_EQ(tmpl_arg.name.GetName(), "T1");

  // A defaulted argument is flagged so it can be hidden from a display name.
  TemplateArgument defaulted;
  defaulted.kind = lldb::eTemplateArgumentKindType;
  defaulted.type = TypeRef(int_type);
  defaulted.is_default = true;
  EXPECT_TRUE(defaulted.is_default);
  EXPECT_EQ(TemplateArgument().kind, lldb::eTemplateArgumentKindNull);
}

// A member function defaults to a plain non-static, non-virtual, unqualified
// method; only its type is required.
TEST_F(TypeCppTest, MemberFunctionDefaults) {
  RecordType fn_type;
  MemberFunction method{TypeRef(fn_type)};
  EXPECT_EQ(&method.type.Get(), &fn_type);
  EXPECT_FALSE(method.is_static);
  EXPECT_FALSE(method.is_const);
  EXPECT_FALSE(method.is_volatile);
  EXPECT_FALSE(method.is_virtual);
  EXPECT_EQ(method.ref_qualifier, RefQualifier::None);
  EXPECT_EQ(method.kind, MemberFunctionKind::Method);

  // The overloadable ref-qualifier and the special-member kind round-trip
  // through the packed bit-fields.
  method.ref_qualifier = RefQualifier::RValue;
  method.kind = MemberFunctionKind::Destructor;
  method.is_virtual = true;
  EXPECT_EQ(method.ref_qualifier, RefQualifier::RValue);
  EXPECT_EQ(method.kind, MemberFunctionKind::Destructor);
  EXPECT_TRUE(method.is_virtual);
}
