//===-- TypeCTest.cpp -----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/TypeC.h"
#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"
#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeCpp.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

namespace {
struct TypeCTest : public testing::Test {
  Context context{
      llvm::cantFail(LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};

  Type *Int() { return context.GetBuiltinType(BuiltinKind::Int); }
  Type *Void() { return context.GetBuiltinType(BuiltinKind::Void); }

  /// A complete record, since several queries below (pointer transparency)
  /// deliberately only apply to complete aggregates.
  RecordType *CompleteRecord(llvm::StringRef name) {
    RecordType *record =
        context.CreateRecordType(name, /*byte_size=*/8, /*is_cpp_class=*/false);
    context.SetComplete(*record);
    return record;
  }
};
} // namespace

// A C struct reports the struct type class, distinguishing it from a C++ class.
TEST_F(TypeCTest, StructTypeClass) {
  RecordType *record = CompleteRecord("MyStruct");
  EXPECT_EQ(record->GetTypeClass(), lldb::eTypeClassStruct);
  EXPECT_TRUE(llvm::isa<StructType>(static_cast<Type *>(record)));
  EXPECT_FALSE(llvm::isa<ClassType>(static_cast<Type *>(record)));
}

// An array's size is its element size times its count, computed on demand so a
// later-known element size is picked up.
TEST_F(TypeCTest, ArraySize) {
  ArrayType *array = context.CreateArrayType(TypeRef(*Int()), 4);
  EXPECT_EQ(array->GetElementType(), Int());
  EXPECT_EQ(array->GetNumElements(), std::optional<uint64_t>(4));
  EXPECT_EQ(array->GetByteSize(), std::optional<uint64_t>(16));
  EXPECT_TRUE(array->IsAggregate());
  EXPECT_EQ(array->GetTypeClass(), lldb::eTypeClassArray);
  EXPECT_EQ(array->GetTypeInfo(),
            uint32_t(lldb::eTypeHasChildren | lldb::eTypeIsArray));
}

// An array of unknown bound has no size at all rather than a size of zero.
TEST_F(TypeCTest, ArrayOfUnknownBound) {
  ArrayType *array = context.CreateArrayType(TypeRef(*Int()), std::nullopt);
  EXPECT_FALSE(array->GetNumElements().has_value());
  EXPECT_FALSE(array->GetByteSize().has_value());

  // A bound can be filled in later (a runtime VLA bound resolved via the DIE).
  array->SetNumElements(2);
  EXPECT_EQ(array->GetByteSize(), std::optional<uint64_t>(8));
}

// A vector (DW_AT_GNU_vector) is laid out like an array but reported as a
// vector so formatting treats it as one.
TEST_F(TypeCTest, Vector) {
  ArrayType *vec = context.CreateArrayType(TypeRef(*Int()), 4);
  vec->SetIsVector(true);
  EXPECT_TRUE(vec->IsVector());
  EXPECT_EQ(vec->GetTypeClass(), lldb::eTypeClassVector);
  EXPECT_EQ(vec->GetTypeInfo(),
            uint32_t(lldb::eTypeHasChildren | lldb::eTypeIsVector));
  // Still sized like an array.
  EXPECT_EQ(vec->GetByteSize(), std::optional<uint64_t>(16));
}

// A pointer takes the target's pointer width from the Context that owns it,
// rather than storing a size of its own -- so even `void *` has a size.
TEST_F(TypeCTest, PointerSizeComesFromContext) {
  PointerType *ptr = context.CreatePointerType(TypeRef(*Int()));
  EXPECT_EQ(ptr->GetPointeeType(), Int());
  EXPECT_EQ(ptr->GetByteSize(),
            std::optional<uint64_t>(context.GetPointerSize()));
  EXPECT_EQ(ptr->GetByteSize(), std::optional<uint64_t>(8));
  // Pointer-aligned, i.e. its own size.
  EXPECT_EQ(ptr->GetAlignmentInBits(), std::optional<uint64_t>(64));

  PointerType *void_ptr = context.CreatePointerType(TypeRef(*Void()));
  EXPECT_EQ(void_ptr->GetByteSize(), std::optional<uint64_t>(8));
}

// A pointer on a 32-bit target is 4 bytes: the width really does follow the
// Context's target rather than being hardcoded.
TEST_F(TypeCTest, PointerSizeFollowsTarget) {
  Context context32{
      llvm::cantFail(LanguageOpts::Create(llvm::Triple("i386-pc-linux-gnu")))};
  PointerType *ptr = context32.CreatePointerType(
      TypeRef(*context32.GetBuiltinType(BuiltinKind::Int)));
  EXPECT_EQ(ptr->GetByteSize(), std::optional<uint64_t>(4));
  EXPECT_EQ(ptr->GetAlignmentInBits(), std::optional<uint64_t>(32));
}

// A pointer to a complete aggregate splices in the pointee's members; one to an
// incomplete pointee (or to a non-aggregate such as `void`) keeps its deref
// child, so that counting children never forces completion.
TEST_F(TypeCTest, PointerTransparency) {
  RecordType *complete = CompleteRecord("Complete");
  PointerType *to_complete = context.CreatePointerType(TypeRef(*complete));
  EXPECT_EQ(to_complete->GetTransparentChildPointee(), complete);
  EXPECT_EQ(to_complete->GetNamedMemberPointee(), complete);

  RecordType *incomplete = context.CreateRecordType(
      "Incomplete", /*byte_size=*/std::nullopt, /*is_cpp_class=*/false);
  PointerType *to_incomplete = context.CreatePointerType(TypeRef(*incomplete));
  EXPECT_EQ(to_incomplete->GetTransparentChildPointee(), nullptr);
  // The by-name path is more permissive: resolving a name may complete it.
  EXPECT_EQ(to_incomplete->GetNamedMemberPointee(), incomplete);

  PointerType *void_ptr = context.CreatePointerType(TypeRef(*Void()));
  EXPECT_EQ(void_ptr->GetTransparentChildPointee(), nullptr);
  EXPECT_EQ(void_ptr->GetNamedMemberPointee(), nullptr);
}

// A pointer to a function is a function pointer; a block pointer never is, even
// though it also points at a function type.
TEST_F(TypeCTest, FunctionPointer) {
  FunctionType *fn =
      context.CreateFunctionType(TypeRef(*Int()), /*is_variadic=*/false);
  PointerType *fn_ptr = context.CreatePointerType(TypeRef(*fn));
  EXPECT_TRUE(fn_ptr->IsFunctionPointer());

  PointerType *int_ptr = context.CreatePointerType(TypeRef(*Int()));
  EXPECT_FALSE(int_ptr->IsFunctionPointer());

  BlockPointerType *block = context.CreateBlockPointerType(TypeRef(*fn));
  EXPECT_FALSE(block->IsFunctionPointer());
  EXPECT_TRUE(llvm::isa<PointerType>(static_cast<Type *>(block)));
}

// A pointer's function-pointer-ness is seen through sugar on the pointee.
TEST_F(TypeCTest, FunctionPointerThroughSugar) {
  FunctionType *fn =
      context.CreateFunctionType(TypeRef(*Int()), /*is_variadic=*/false);
  TypedefType *alias = context.CreateTypedefType("fn_t", TypeRef(*fn));
  PointerType *ptr = context.CreatePointerType(TypeRef(*alias));
  EXPECT_TRUE(ptr->IsFunctionPointer());
}

// A typedef is sugar: it reports its own name and type class but forwards
// layout to what it aliases, and desugars through to the canonical type.
TEST_F(TypeCTest, Typedef) {
  TypedefType *alias = context.CreateTypedefType("my_int", TypeRef(*Int()));
  EXPECT_EQ(alias->GetName().GetName(), "my_int");
  EXPECT_EQ(alias->GetUnderlyingType(), Int());
  EXPECT_EQ(alias->GetByteSize(), Int()->GetByteSize());
  EXPECT_EQ(alias->GetTypeClass(), lldb::eTypeClassTypedef);
  EXPECT_EQ(alias->Desugar(), Int());
  EXPECT_TRUE(llvm::isa<SugarType>(static_cast<Type *>(alias)));
}

// A cv-qualified type is sugar that records which qualifiers apply, and the
// CVR mask collapses a whole chain of them.
TEST_F(TypeCTest, CVQualified) {
  CVQualifiedType *c =
      context.CreateCVQualifiedType(TypeRef(*Int()), /*is_const=*/true,
                                    /*is_volatile=*/false);
  EXPECT_TRUE(c->IsConst());
  EXPECT_FALSE(c->IsVolatile());
  EXPECT_EQ(c->Desugar(), Int());
  EXPECT_EQ(c->GetByteSize(), Int()->GetByteSize());
  EXPECT_EQ(CVQualifiedType::GetCVRMask(c), 0x1u);

  // `const volatile int` through a chain of two qualifiers.
  CVQualifiedType *cv =
      context.CreateCVQualifiedType(TypeRef(*c), /*is_const=*/false,
                                    /*is_volatile=*/true);
  EXPECT_EQ(CVQualifiedType::GetCVRMask(cv), 0x1u | 0x4u);
  EXPECT_EQ(cv->Desugar(), Int());

  // A non-qualified type carries no qualifiers.
  EXPECT_EQ(CVQualifiedType::GetCVRMask(Int()), 0u);
  EXPECT_EQ(CVQualifiedType::GetCVRMask(nullptr), 0u);
}

// An enum has an underlying integer type it takes its encoding from, and
// enumerators added through the Context.
TEST_F(TypeCTest, Enum) {
  EnumType *e = context.CreateEnumType("Color", /*byte_size=*/4,
                                       TypeRef(*Int()), /*is_scoped=*/false);
  EXPECT_EQ(e->GetName().GetName(), "Color");
  EXPECT_EQ(e->GetUnderlyingType(), Int());
  EXPECT_EQ(e->GetByteSize(), std::optional<uint64_t>(4));
  EXPECT_EQ(e->GetFormat(), lldb::eFormatEnum);
  EXPECT_EQ(e->GetTypeClass(), lldb::eTypeClassEnumeration);
  // Matches TypeSystemClang: an enum is NOT flagged as a scalar.
  EXPECT_EQ(e->GetTypeInfo(),
            uint32_t(lldb::eTypeIsEnumeration | lldb::eTypeHasValue));
  // `int` is signed, so the enum is too, and it inherits its encoding.
  EXPECT_TRUE(e->IsSigned());
  EXPECT_EQ(e->GetEncoding(), Int()->GetEncoding());
  EXPECT_FALSE(e->IsScoped());
  EXPECT_TRUE(e->GetEnumerators().empty());

  context.AddEnumerator(*e, context.GetIdentifier("Red"), 0);
  context.AddEnumerator(*e, context.GetIdentifier("Green"), 1);
  ASSERT_EQ(e->GetEnumerators().size(), 2u);
  EXPECT_EQ(e->GetEnumerators()[0].name.GetName(), "Red");
  EXPECT_EQ(e->GetEnumerators()[1].value, 1u);

  e->SetIsScoped(true);
  EXPECT_TRUE(e->IsScoped());
}

// A function type carries its return type, parameters and variadic-ness, and
// has no size of its own.
TEST_F(TypeCTest, Function) {
  FunctionType *fn =
      context.CreateFunctionType(TypeRef(*Int()), /*is_variadic=*/false);
  EXPECT_EQ(fn->GetReturnType(), Int());
  EXPECT_FALSE(fn->IsVariadic());
  EXPECT_EQ(fn->GetNumParameters(), 0u);
  EXPECT_FALSE(fn->GetByteSize().has_value());
  EXPECT_EQ(fn->GetTypeClass(), lldb::eTypeClassFunction);

  context.AddParameter(*fn, TypeRef(*Int()));
  context.AddParameter(*fn, TypeRef(*context.CreatePointerType(
                                 TypeRef(*Void()))));
  EXPECT_EQ(fn->GetNumParameters(), 2u);
  EXPECT_EQ(fn->GetParameterAtIndex(0), Int());

  FunctionType *variadic =
      context.CreateFunctionType(TypeRef(*Int()), /*is_variadic=*/true);
  EXPECT_TRUE(variadic->IsVariadic());
}

// A `_Complex T` is two contiguous components, so twice the element size.
TEST_F(TypeCTest, Complex) {
  Type *dbl = context.GetBuiltinType(BuiltinKind::Double);
  ComplexType *cplx = context.CreateComplexType(TypeRef(*dbl));
  EXPECT_EQ(cplx->GetElementType(), dbl);
  ASSERT_TRUE(dbl->GetByteSize().has_value());
  EXPECT_EQ(cplx->GetByteSize(), std::optional<uint64_t>(*dbl->GetByteSize() * 2));
  EXPECT_EQ(cplx->GetTypeClass(), lldb::eTypeClassComplexFloat);
}

// A pointer-to-member-function is told apart from a pointer-to-data-member by
// what it points at -- the check that needs FunctionType, hence lands here.
TEST_F(TypeCTest, MemberPointerFunctionVsData) {
  RecordType *containing = CompleteRecord("C");
  FunctionType *fn =
      context.CreateFunctionType(TypeRef(*Int()), /*is_variadic=*/false);

  MemberPointerType *to_fn = context.CreateMemberPointerType(
      TypeRef(*fn), TypeRef(*containing));
  EXPECT_TRUE(to_fn->IsMemberFunctionPointer());

  MemberPointerType *to_data = context.CreateMemberPointerType(
      TypeRef(*Int()), TypeRef(*containing));
  EXPECT_FALSE(to_data->IsMemberFunctionPointer());

  // Seen through sugar on the pointee, too.
  TypedefType *alias = context.CreateTypedefType("fn_t", TypeRef(*fn));
  MemberPointerType *to_alias = context.CreateMemberPointerType(
      TypeRef(*alias), TypeRef(*containing));
  EXPECT_TRUE(to_alias->IsMemberFunctionPointer());
}

// An HFA is a record whose direct fields are all the same floating-point type;
// an HVA the same for vectors. A mix, or any other field kind, disqualifies it.
TEST_F(TypeCTest, HomogeneousAggregateBase) {
  Type *flt = context.GetBuiltinType(BuiltinKind::Float);
  uint32_t num_fields = 0;

  RecordType *hfa = context.CreateRecordType("HFA", 8, /*is_cpp_class=*/false);
  context.AddField(*hfa, context.GetIdentifier("a"), TypeRef(*flt), 0);
  context.AddField(*hfa, context.GetIdentifier("b"), TypeRef(*flt), 4);
  context.SetComplete(*hfa);
  EXPECT_EQ(hfa->GetHomogeneousAggregateBase(num_fields), flt);
  EXPECT_EQ(num_fields, 2u);

  // A mix of a float and an int is not homogeneous.
  RecordType *mixed = context.CreateRecordType("Mixed", 8,
                                               /*is_cpp_class=*/false);
  context.AddField(*mixed, context.GetIdentifier("a"), TypeRef(*flt), 0);
  context.AddField(*mixed, context.GetIdentifier("b"), TypeRef(*Int()), 4);
  context.SetComplete(*mixed);
  EXPECT_EQ(mixed->GetHomogeneousAggregateBase(num_fields), nullptr);
  EXPECT_EQ(num_fields, 0u);

  // An empty record has no base type either.
  RecordType *empty = CompleteRecord("Empty");
  EXPECT_EQ(empty->GetHomogeneousAggregateBase(num_fields), nullptr);
  EXPECT_EQ(num_fields, 0u);
}

// An HVA's fields must be the same vector type with matching width; two vectors
// of different widths disqualify the record.
TEST_F(TypeCTest, HomogeneousVectorAggregate) {
  Type *flt = context.GetBuiltinType(BuiltinKind::Float);
  ArrayType *vec4 = context.CreateArrayType(TypeRef(*flt), 4);
  vec4->SetIsVector(true);
  uint32_t num_fields = 0;

  RecordType *hva = context.CreateRecordType("HVA", 32,
                                             /*is_cpp_class=*/false);
  context.AddField(*hva, context.GetIdentifier("a"), TypeRef(*vec4), 0);
  context.AddField(*hva, context.GetIdentifier("b"), TypeRef(*vec4), 16);
  context.SetComplete(*hva);
  EXPECT_EQ(hva->GetHomogeneousAggregateBase(num_fields), vec4);
  EXPECT_EQ(num_fields, 2u);

  ArrayType *vec2 = context.CreateArrayType(TypeRef(*flt), 2);
  vec2->SetIsVector(true);
  RecordType *ragged = context.CreateRecordType("Ragged", 24,
                                                /*is_cpp_class=*/false);
  context.AddField(*ragged, context.GetIdentifier("a"), TypeRef(*vec4), 0);
  context.AddField(*ragged, context.GetIdentifier("b"), TypeRef(*vec2), 16);
  context.SetComplete(*ragged);
  EXPECT_EQ(ragged->GetHomogeneousAggregateBase(num_fields), nullptr);
}
