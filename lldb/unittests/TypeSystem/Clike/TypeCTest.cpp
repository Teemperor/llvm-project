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

#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

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
        context.CreateRecordType(name, /*byte_size=*/8);
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
}

// An array's size is its element size times its count, computed on demand so a
// later-known element size is picked up.
TEST_F(TypeCTest, ArraySize) {
  ArrayType *array = context.CreateArrayType(TypeRef(*Int()), 4);
  EXPECT_EQ(array->GetElementType(), Int());
  EXPECT_EQ(array->GetArrayElementType(), Int());
  EXPECT_EQ(array->GetNumElements(), std::optional<uint64_t>(4));
  EXPECT_FALSE(array->IsVector());
  EXPECT_EQ(array->GetDIEUID(), UINT64_MAX);
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

  // The DIE it came from is remembered, so a runtime (VLA) bound can be
  // re-resolved per frame.
  ArrayType *vla = context.CreateArrayType(TypeRef(*Int()), std::nullopt,
                                           /*is_vector=*/false, /*die_uid=*/42);
  EXPECT_EQ(vla->GetDIEUID(), 42u);
}

// A vector (DW_AT_GNU_vector) is laid out like an array but reported as a
// vector so formatting treats it as one.
TEST_F(TypeCTest, Vector) {
  ArrayType *vec =
      context.CreateArrayType(TypeRef(*Int()), 4, /*is_vector=*/true);
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
  EXPECT_TRUE(ptr->IsPointer());
  EXPECT_FALSE(ptr->IsBlockPointer());
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
      "Incomplete", /*byte_size=*/std::nullopt);
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
      context.CreateFunctionType(TypeRef(*Int()), {}, /*is_variadic=*/false);
  PointerType *fn_ptr = context.CreatePointerType(TypeRef(*fn));
  EXPECT_TRUE(fn_ptr->IsFunctionPointer());

  PointerType *int_ptr = context.CreatePointerType(TypeRef(*Int()));
  EXPECT_FALSE(int_ptr->IsFunctionPointer());

  BlockPointerType *block = context.CreateBlockPointerType(TypeRef(*fn));
  EXPECT_FALSE(block->IsFunctionPointer());
  EXPECT_TRUE(block->IsBlockPointer());
  EXPECT_TRUE(block->IsPointer());
  EXPECT_TRUE(llvm::isa<PointerType>(static_cast<Type *>(block)));
}

// A pointer to a record is a C++ construct (so class formatters fire); a
// pointer to anything else a C one.
TEST_F(TypeCTest, PointerMinimumLanguage) {
  EXPECT_EQ(context.CreatePointerType(TypeRef(*Int()))->GetMinimumLanguage(),
            lldb::eLanguageTypeC);
  RecordType *record = CompleteRecord("Rec");
  EXPECT_EQ(context.CreatePointerType(TypeRef(*record))->GetMinimumLanguage(),
            lldb::eLanguageTypeC_plus_plus);
  // Seen through sugar on the pointee.
  TypedefType *alias = context.CreateTypedefType("Rec_t", TypeRef(*record));
  EXPECT_EQ(context.CreatePointerType(TypeRef(*alias))->GetMinimumLanguage(),
            lldb::eLanguageTypeC_plus_plus);
}

// A `void *` is always a possible dynamic type; a pointer to a C struct or a
// scalar never is.
TEST_F(TypeCTest, PointerDynamicTypeCandidate) {
  auto complete = [](Type *) {};
  PointerType *void_ptr = context.CreatePointerType(TypeRef(*Void()));
  EXPECT_EQ(void_ptr->GetDynamicTypeCandidate(/*check_cplusplus=*/false,
                                              /*check_objc=*/false, complete),
            Void());
  PointerType *int_ptr = context.CreatePointerType(TypeRef(*Int()));
  EXPECT_EQ(int_ptr->GetDynamicTypeCandidate(true, true, complete), nullptr);
  RecordType *record = CompleteRecord("CStruct");
  PointerType *record_ptr = context.CreatePointerType(TypeRef(*record));
  EXPECT_EQ(record_ptr->GetDynamicTypeCandidate(true, true, complete), nullptr);
}

// A pointer's function-pointer-ness is seen through sugar on the pointee.
TEST_F(TypeCTest, FunctionPointerThroughSugar) {
  FunctionType *fn =
      context.CreateFunctionType(TypeRef(*Int()), {}, /*is_variadic=*/false);
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
  EXPECT_EQ(alias->GetTypedefedType(), Int());
  EXPECT_TRUE(llvm::isa<SugarType>(static_cast<Type *>(alias)));
}

// Elaborated sugar is peeled by StripElaborated, which stops at a typedef.
TEST_F(TypeCTest, Elaborated) {
  TypedefType *alias = context.CreateTypedefType("my_int", TypeRef(*Int()));
  ElaboratedType *el =
      context.CreateElaboratedType("::my_int", TypeRef(*alias));
  ElaboratedType *el_el = context.CreateElaboratedType("::my_int", TypeRef(*el));
  EXPECT_EQ(el->GetSpelling().GetName(), "::my_int");
  EXPECT_EQ(el_el->StripElaborated(), alias);
  EXPECT_EQ(alias->StripElaborated(), alias);
  // An elaborated type isn't itself a typedef.
  EXPECT_EQ(el->GetTypedefedType(), nullptr);
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
  EXPECT_EQ(c->GetCVRQualifiers(), 0x1u);

  // `const volatile int` through a chain of two qualifiers.
  CVQualifiedType *cv =
      context.CreateCVQualifiedType(TypeRef(*c), /*is_const=*/false,
                                    /*is_volatile=*/true);
  EXPECT_EQ(cv->GetCVRQualifiers(), 0x1u | 0x4u);
  EXPECT_EQ(cv->Desugar(), Int());
  EXPECT_EQ(cv->StripCVQualifiers(), Int());
  // IsConst only looks at the outermost qualifier.
  EXPECT_FALSE(cv->IsConst());

  // A non-qualified type carries no qualifiers.
  EXPECT_EQ(Int()->GetCVRQualifiers(), 0u);

  // Only cv-sugar at the top counts: a typedef stops both the mask and the
  // stripping.
  TypedefType *alias = context.CreateTypedefType("cint", TypeRef(*c));
  EXPECT_EQ(alias->GetCVRQualifiers(), 0u);
  CVQualifiedType *v_alias =
      context.CreateCVQualifiedType(TypeRef(*alias), /*is_const=*/false,
                                    /*is_volatile=*/true);
  EXPECT_EQ(v_alias->GetCVRQualifiers(), 0x4u);
  EXPECT_EQ(v_alias->StripCVQualifiers(), alias);
}

// A `__ptrauth` qualifier is reported through any sugar wrapped around it, but
// not through a pointer (it qualifies the pointer, not its pointee).
TEST_F(TypeCTest, PtrAuth) {
  PointerType *ptr = context.CreatePointerType(TypeRef(*Int()));
  PtrAuthType *signed_ptr = context.CreatePtrAuthType(
      TypeRef(*ptr), /*key=*/2, /*addr_discriminated=*/true,
      /*extra_discriminator=*/42);
  EXPECT_EQ(signed_ptr->GetKey(), 2u);
  EXPECT_TRUE(signed_ptr->IsAddressDiscriminated());
  EXPECT_EQ(signed_ptr->GetExtraDiscriminator(), 42u);

  TypedefType *alias = context.CreateTypedefType("sp", TypeRef(*signed_ptr));
  std::optional<PtrAuthQualifier> q = alias->GetPtrAuthQualifier();
  ASSERT_TRUE(q.has_value());
  EXPECT_EQ(q->GetKey(), 2u);
  EXPECT_TRUE(q->IsAddressDiscriminated());
  EXPECT_EQ(q->GetExtraDiscriminator(), 42u);

  EXPECT_FALSE(ptr->GetPtrAuthQualifier().has_value());
  PointerType *to_signed = context.CreatePointerType(TypeRef(*signed_ptr));
  EXPECT_FALSE(to_signed->GetPtrAuthQualifier().has_value());
}

// An enum has an underlying integer type it takes its encoding from, and the
// enumerators it was created with.
TEST_F(TypeCTest, Enum) {
  EnumType *e = context.CreateEnumType("Color", /*byte_size=*/4,
                                       TypeRef(*Int()), /*is_scoped=*/false);
  EXPECT_EQ(e->GetName().GetName(), "Color");
  EXPECT_EQ(e->GetUnderlyingType(), Int());
  EXPECT_EQ(e->GetEnumerationIntegerType(), Int());
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
  EXPECT_FALSE(e->IsScopedEnumeration());
  EXPECT_TRUE(e->GetEnumerators().empty());
  // An unscoped enum is an integer that promotes like one.
  EXPECT_TRUE(e->IsInteger());
  EXPECT_TRUE(e->IsPromotableInteger());

  EnumType *colors = context.CreateEnumType(
      "Colors", /*byte_size=*/4, TypeRef(*Int()), /*is_scoped=*/true,
      {{context.GetIdentifier("Red"), 0}, {context.GetIdentifier("Green"), 1}});
  ASSERT_EQ(colors->GetEnumerators().size(), 2u);
  EXPECT_EQ(colors->GetEnumerators()[0].GetName().GetName(), "Red");
  EXPECT_EQ(colors->GetEnumerators()[1].GetValue(), 1u);
  EXPECT_TRUE(colors->IsScoped());
  EXPECT_TRUE(colors->IsScopedEnumeration());
  // A scoped enum doesn't promote implicitly.
  EXPECT_FALSE(colors->IsPromotableInteger());
}

// A function type carries its return type, parameters and variadic-ness, and
// has no size of its own.
TEST_F(TypeCTest, Function) {
  FunctionType *fn =
      context.CreateFunctionType(TypeRef(*Int()), {}, /*is_variadic=*/false);
  EXPECT_TRUE(fn->IsFunction());
  EXPECT_EQ(fn->GetReturnType(), Int());
  EXPECT_FALSE(fn->IsVariadic());
  EXPECT_EQ(fn->GetNumParameters(), 0u);
  EXPECT_FALSE(fn->GetByteSize().has_value());
  EXPECT_EQ(fn->GetTypeClass(), lldb::eTypeClassFunction);

  Type *void_ptr = context.CreatePointerType(TypeRef(*Void()));
  FunctionType *with_params = context.CreateFunctionType(
      TypeRef(*Int()), {TypeRef(*Int()), TypeRef(*void_ptr)},
      /*is_variadic=*/false, /*use_void_for_empty_params=*/false,
      {context.GetIdentifier("x")});
  EXPECT_EQ(with_params->GetNumParameters(), 2u);
  EXPECT_EQ(with_params->GetParameterAtIndex(0), Int());
  EXPECT_EQ(with_params->GetParameterAtIndex(1), void_ptr);
  EXPECT_EQ(with_params->GetParameterAtIndex(2), nullptr);
  // Names not given are empty.
  EXPECT_EQ(with_params->GetParameterNameAtIndex(0), "x");
  EXPECT_EQ(with_params->GetParameterNameAtIndex(1), "");

  FunctionType *variadic =
      context.CreateFunctionType(TypeRef(*Int()), {}, /*is_variadic=*/true);
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

// An HFA is a record whose direct fields are all the same floating-point type;
// an HVA the same for vectors. A mix, or any other field kind, disqualifies it.
TEST_F(TypeCTest, HomogeneousAggregateBase) {
  Type *flt = context.GetBuiltinType(BuiltinKind::Float);
  uint32_t num_fields = 0;

  RecordType *hfa = context.CreateRecordType("HFA", 8);
  context.AddField(*hfa, context.GetIdentifier("a"), TypeRef(*flt), 0);
  context.AddField(*hfa, context.GetIdentifier("b"), TypeRef(*flt), 4);
  context.SetComplete(*hfa);
  EXPECT_EQ(hfa->GetHomogeneousAggregateBase(num_fields), flt);
  EXPECT_EQ(num_fields, 2u);

  // A mix of a float and an int is not homogeneous.
  RecordType *mixed = context.CreateRecordType("Mixed", 8);
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
  ArrayType *vec4 =
      context.CreateArrayType(TypeRef(*flt), 4, /*is_vector=*/true);
  uint32_t num_fields = 0;

  RecordType *hva = context.CreateRecordType("HVA", 32);
  context.AddField(*hva, context.GetIdentifier("a"), TypeRef(*vec4), 0);
  context.AddField(*hva, context.GetIdentifier("b"), TypeRef(*vec4), 16);
  context.SetComplete(*hva);
  EXPECT_EQ(hva->GetHomogeneousAggregateBase(num_fields), vec4);
  EXPECT_EQ(num_fields, 2u);

  ArrayType *vec2 =
      context.CreateArrayType(TypeRef(*flt), 2, /*is_vector=*/true);
  RecordType *ragged = context.CreateRecordType("Ragged", 24);
  context.AddField(*ragged, context.GetIdentifier("a"), TypeRef(*vec4), 0);
  context.AddField(*ragged, context.GetIdentifier("b"), TypeRef(*vec2), 16);
  context.SetComplete(*ragged);
  EXPECT_EQ(ragged->GetHomogeneousAggregateBase(num_fields), nullptr);
}
