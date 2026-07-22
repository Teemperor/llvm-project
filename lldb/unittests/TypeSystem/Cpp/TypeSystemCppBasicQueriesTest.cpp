//===-- TypeSystemCppBasicQueriesTest.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Cpp/Builder.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

namespace {
struct TypeSystemCppBasicQueriesTest : public testing::Test {
  std::shared_ptr<TypeSystemCpp> ts = std::make_shared<TypeSystemCpp>(
      "test", llvm::Triple("x86_64-pc-linux-gnu"));
  Builder builder{*ts};

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }
  CompilerType GetChar() {
    return builder.GetBuiltinType("char", 1, lldb::eEncodingSint,
                                  lldb::eFormatChar);
  }
  CompilerType GetVoid() { return builder.GetVoidType(); }
};
} // namespace

// A record is an aggregate type, a builtin is not.
TEST_F(TypeSystemCppBasicQueriesTest, IsAggregateType) {
  CompilerType record = builder.CreateRecordType("Foo", 4, false);
  EXPECT_TRUE(record.IsAggregateType());
  EXPECT_FALSE(GetInt().IsAggregateType());
}

// IsCharType recognizes the narrow char builtins by format+size, but not int
// (which shares the signed-integer encoding).
TEST_F(TypeSystemCppBasicQueriesTest, IsCharType) {
  EXPECT_TRUE(GetChar().IsCharType());
  EXPECT_FALSE(GetInt().IsCharType());
}

// A cv-qualified char (e.g. the pointee of `const char *`) is still
// recognized as a char type: IsCharType desugars.
TEST_F(TypeSystemCppBasicQueriesTest, IsCharTypeThroughSugar) {
  CompilerType const_char =
      builder.CreateCVQualifiedType(GetChar(), /*is_const=*/true,
                                    /*is_volatile=*/false);
  EXPECT_TRUE(const_char.IsCharType());
}

// A floating point builtin reports IsFloatingPointType; an integer builtin
// does not.
TEST_F(TypeSystemCppBasicQueriesTest, IsFloatingPointType) {
  CompilerType f = builder.GetBuiltinType("float", 4,
                                          lldb::eEncodingIEEE754,
                                          lldb::eFormatFloat);
  EXPECT_TRUE(f.IsFloatingPointType());
  EXPECT_FALSE(GetInt().IsFloatingPointType());
}

// A FunctionType (and a typedef of one) reports as a function type.
TEST_F(TypeSystemCppBasicQueriesTest, IsFunctionType) {
  CompilerType fn = builder.CreateFunctionType(GetVoid(), /*is_variadic=*/false);
  EXPECT_TRUE(fn.IsFunctionType());
  CompilerType alias = builder.CreateTypedefType("Fn", fn);
  EXPECT_TRUE(alias.IsFunctionType());
  EXPECT_FALSE(GetInt().IsFunctionType());
}

// Only a *pointer* to a function is a function-pointer type; a reference to
// one is not (mirrors clang's isFunctionPointerType()).
TEST_F(TypeSystemCppBasicQueriesTest, IsFunctionPointerType) {
  CompilerType fn = builder.CreateFunctionType(GetVoid(), false);
  CompilerType fn_ptr = builder.CreatePointerType(fn);
  CompilerType fn_ref = builder.CreateReferenceType(fn, /*is_rvalue=*/false);
  EXPECT_TRUE(fn_ptr.IsFunctionPointerType());
  EXPECT_FALSE(fn_ref.IsFunctionPointerType());
  EXPECT_FALSE(GetInt().IsFunctionPointerType());
}

// A block pointer is a pointer but reports IsBlockPointerType and hands back
// the equivalent plain function-pointer type; a plain pointer does not.
TEST_F(TypeSystemCppBasicQueriesTest, IsBlockPointerType) {
  CompilerType fn = builder.CreateFunctionType(GetVoid(), false);
  CompilerType block_ptr = builder.CreateBlockPointerType(fn);
  CompilerType function_pointer_type;
  EXPECT_TRUE(ts->IsBlockPointerType(block_ptr.GetOpaqueQualType(),
                                     &function_pointer_type));
  EXPECT_TRUE(function_pointer_type.IsFunctionPointerType());

  CompilerType plain_ptr = builder.CreatePointerType(GetInt());
  EXPECT_FALSE(ts->IsBlockPointerType(plain_ptr.GetOpaqueQualType(), nullptr));
}

// Signed and unsigned integer builtins report IsIntegerType with the correct
// signedness; nullptr_t (despite an unsigned encoding) does not.
TEST_F(TypeSystemCppBasicQueriesTest, IsIntegerType) {
  bool is_signed = false;
  EXPECT_TRUE(ts->IsIntegerType(GetInt().GetOpaqueQualType(), is_signed));
  EXPECT_TRUE(is_signed);

  CompilerType unsigned_int = builder.GetBuiltinType(
      "unsigned int", 4, lldb::eEncodingUint,
      lldb::eFormatUnsigned);
  EXPECT_TRUE(ts->IsIntegerType(unsigned_int.GetOpaqueQualType(), is_signed));
  EXPECT_FALSE(is_signed);

  CompilerType nullptr_t = builder.GetBuiltinType(
      "std::nullptr_t", 8, lldb::eEncodingUint, lldb::eFormatHex);
  EXPECT_FALSE(ts->IsIntegerType(nullptr_t.GetOpaqueQualType(), is_signed));
}

// IsScopedEnumerationType / IsEnumerationType distinguish `enum class` from a
// plain `enum`, and report the correct signedness derived from the underlying
// type.
TEST_F(TypeSystemCppBasicQueriesTest, EnumerationQueries) {
  CompilerType plain_enum =
      builder.CreateEnumType("E", 4, GetInt(), /*is_scoped=*/false);
  CompilerType scoped_enum = builder.CreateEnumType(
      "ScopedE", 4, GetInt(), /*is_scoped=*/true);

  EXPECT_FALSE(ts->IsScopedEnumerationType(plain_enum.GetOpaqueQualType()));
  EXPECT_TRUE(ts->IsScopedEnumerationType(scoped_enum.GetOpaqueQualType()));

  bool is_signed = false;
  EXPECT_TRUE(ts->IsEnumerationType(plain_enum.GetOpaqueQualType(), is_signed));
  EXPECT_TRUE(is_signed);
  EXPECT_FALSE(ts->IsEnumerationType(GetInt().GetOpaqueQualType(), is_signed));
}

// IsPointerType reports the pointee type through the out-parameter.
TEST_F(TypeSystemCppBasicQueriesTest, IsPointerType) {
  CompilerType ptr = builder.CreatePointerType(GetInt());
  CompilerType pointee;
  EXPECT_TRUE(ts->IsPointerType(ptr.GetOpaqueQualType(), &pointee));
  EXPECT_EQ(pointee.GetOpaqueQualType(), GetInt().GetOpaqueQualType());
  EXPECT_FALSE(ts->IsPointerType(GetInt().GetOpaqueQualType(), nullptr));
}

// A builtin int is a scalar type; a record is not.
TEST_F(TypeSystemCppBasicQueriesTest, IsScalarType) {
  EXPECT_TRUE(GetInt().IsScalarType());
  CompilerType record = builder.CreateRecordType("Foo", 4, false);
  EXPECT_FALSE(record.IsScalarType());
}

// IsVoidType identifies the void builtin by spelling, including through
// typedef sugar, but not other builtins.
TEST_F(TypeSystemCppBasicQueriesTest, IsVoidType) {
  EXPECT_TRUE(GetVoid().IsVoidType());
  CompilerType alias = builder.CreateTypedefType("MyVoid", GetVoid());
  EXPECT_TRUE(alias.IsVoidType());
  EXPECT_FALSE(GetInt().IsVoidType());
}

// A pointer to a polymorphic class is a possible dynamic type when checking
// C++; a pointer to a non-polymorphic class is not.
TEST_F(TypeSystemCppBasicQueriesTest, IsPossibleDynamicTypePolymorphic) {
  CompilerType poly = builder.CreateRecordType("Poly", 8, true);
  auto *poly_class =
      llvm::cast<ClassType>(static_cast<cpp_typesystem::Type *>(poly.GetOpaqueQualType()));
  builder.SetRecordComplete(*poly_class);
  builder.SetRecordPolymorphic(*poly_class);
  CompilerType poly_ptr = builder.CreatePointerType(poly);

  CompilerType target;
  EXPECT_TRUE(ts->IsPossibleDynamicType(poly_ptr.GetOpaqueQualType(), &target,
                                        /*check_cplusplus=*/true,
                                        /*check_objc=*/false));
  EXPECT_EQ(target.GetOpaqueQualType(), poly.GetOpaqueQualType());

  CompilerType plain = builder.CreateRecordType("Plain", 4, true);
  builder.SetRecordComplete(
      *llvm::cast<ClassType>(static_cast<cpp_typesystem::Type *>(plain.GetOpaqueQualType())));
  CompilerType plain_ptr = builder.CreatePointerType(plain);
  EXPECT_FALSE(ts->IsPossibleDynamicType(plain_ptr.GetOpaqueQualType(), nullptr,
                                         /*check_cplusplus=*/true,
                                         /*check_objc=*/false));
}
