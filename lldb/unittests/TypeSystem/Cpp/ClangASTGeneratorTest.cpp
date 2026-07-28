//===-- ClangASTGeneratorTest.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangASTGeneratorTestUtils.h"

#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeC.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/Type.h"

#include "llvm/Support/Casting.h"

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

namespace {
using ClangASTGeneratorTest = ClangASTGeneratorTestUtils;
} // namespace

// Generating a builtin cpp type produces the matching clang builtin QualType.
TEST_F(ClangASTGeneratorTest, GenerateBuiltinInt) {
  ClangASTGenerator generator(ast);
  clang::QualType qt = generator.Generate(GetInt());
  ASSERT_FALSE(qt.isNull());
  EXPECT_EQ(qt, ast.IntTy);
}

// Generating the same cpp type twice returns the identical clang QualType
// (the generator caches by cpp type).
TEST_F(ClangASTGeneratorTest, GenerateIsCached) {
  ClangASTGenerator generator(ast);
  CompilerType record = builder.CreateRecordType("Foo", 4, false);
  clang::QualType a = generator.Generate(record);
  clang::QualType b = generator.Generate(record);
  EXPECT_EQ(a, b);
}

// A record is generated as a forward declaration (no external lexical storage
// query needed until asked); its name and tag kind (struct/class) are
// preserved.
TEST_F(ClangASTGeneratorTest, GenerateRecordForwardDeclaration) {
  ClangASTGenerator generator(ast);
  CompilerType record =
      builder.CreateRecordType("MyStruct", 4, /*is_cpp_class=*/false);
  clang::QualType qt = generator.Generate(record);
  ASSERT_FALSE(qt.isNull());
  clang::CXXRecordDecl *decl = qt->getAsCXXRecordDecl();
  ASSERT_NE(decl, nullptr);
  EXPECT_EQ(decl->getNameAsString(), "MyStruct");
  EXPECT_EQ(decl->getTagKind(), clang::TagTypeKind::Struct);
  EXPECT_FALSE(decl->isCompleteDefinition());

  CompilerType cls = builder.CreateRecordType("MyClass", 4,
                                              /*is_cpp_class=*/true,
                                              /*is_union=*/false,
                                              /*is_class_keyword=*/true);
  clang::QualType cls_qt = generator.Generate(cls);
  EXPECT_EQ(cls_qt->getAsCXXRecordDecl()->getTagKind(),
           clang::TagTypeKind::Class);
}

// CompleteRecord fills in a forward-declared record's fields/bases (here just
// one int field), matching the source cpp record's layout.
TEST_F(ClangASTGeneratorTest, CompleteRecordAddsFields) {
  ClangASTGenerator generator(ast);
  CompilerType record = builder.CreateRecordType("Foo", 4, false);
  auto *r =
      llvm::cast<RecordType>(static_cast<cpp_typesystem::Type *>(record.GetOpaqueQualType()));
  builder.AddField(*r, builder.GetIdentifier("x"),
                   static_cast<cpp_typesystem::Type *>(GetInt().GetOpaqueQualType()), 0);
  builder.SetRecordComplete(*r);

  clang::QualType qt = generator.Generate(record);
  clang::TagDecl *tag = qt->getAsTagDecl();
  ASSERT_NE(tag, nullptr);
  ASSERT_TRUE(generator.CompleteRecord(tag));
  EXPECT_TRUE(tag->isCompleteDefinition());
  auto *cxx_record = llvm::cast<clang::CXXRecordDecl>(tag);
  EXPECT_EQ(std::distance(cxx_record->field_begin(), cxx_record->field_end()),
           1);
  EXPECT_EQ((*cxx_record->field_begin())->getNameAsString(), "x");
}

// Records with the same fully-qualified name are unified: generating two
// different cpp RecordType instances (e.g. from separate modules) sharing a
// name produces the same clang decl, so overload resolution treats them as
// the same type.
TEST_F(ClangASTGeneratorTest, RecordsUnifiedByName) {
  ClangASTGenerator generator(ast);
  CompilerType a = builder.CreateRecordType("Shared", 4, false);
  CompilerType b = builder.CreateRecordType("Shared", 4, false);
  clang::QualType qa = generator.Generate(a);
  clang::QualType qb = generator.Generate(b);
  EXPECT_EQ(qa, qb);
}

// A pointer/reference cpp type generates the matching clang pointer/reference
// type over the recursively-generated pointee.
TEST_F(ClangASTGeneratorTest, GeneratePointerAndReference) {
  ClangASTGenerator generator(ast);
  CompilerType ptr = builder.CreatePointerType(GetInt());
  clang::QualType ptr_qt = generator.Generate(ptr);
  ASSERT_TRUE(ptr_qt->isPointerType());
  EXPECT_EQ(ptr_qt->getPointeeType(), ast.IntTy);

  CompilerType lref = builder.CreateReferenceType(GetInt(), /*is_rvalue=*/false);
  clang::QualType lref_qt = generator.Generate(lref);
  EXPECT_TRUE(lref_qt->isLValueReferenceType());

  CompilerType rref = builder.CreateReferenceType(GetInt(), /*is_rvalue=*/true);
  clang::QualType rref_qt = generator.Generate(rref);
  EXPECT_TRUE(rref_qt->isRValueReferenceType());
}

// A cv-qualified cpp type generates a clang QualType carrying the matching
// local qualifiers.
TEST_F(ClangASTGeneratorTest, GenerateCVQualified) {
  ClangASTGenerator generator(ast);
  CompilerType const_int =
      builder.CreateCVQualifiedType(GetInt(), /*is_const=*/true,
                                    /*is_volatile=*/false);
  clang::QualType qt = generator.Generate(const_int);
  EXPECT_TRUE(qt.isConstQualified());
  EXPECT_FALSE(qt.isVolatileQualified());
}

// A bounded array generates a clang ConstantArrayType with the matching
// element count; an unbounded array generates an IncompleteArrayType.
TEST_F(ClangASTGeneratorTest, GenerateArrays) {
  ClangASTGenerator generator(ast);
  CompilerType bounded = builder.CreateArrayType(GetInt(), 4);
  clang::QualType bounded_qt = generator.Generate(bounded);
  auto *cat = llvm::dyn_cast<clang::ConstantArrayType>(bounded_qt.getTypePtr());
  ASSERT_NE(cat, nullptr);
  EXPECT_EQ(cat->getSize(), 4u);

  CompilerType unbounded = builder.CreateArrayType(GetInt(), std::nullopt);
  clang::QualType unbounded_qt = generator.Generate(unbounded);
  EXPECT_TRUE(llvm::isa<clang::IncompleteArrayType>(unbounded_qt.getTypePtr()));
}

// A typedef generates a clang TypedefType wrapping the (recursively
// generated) underlying type, with its own name preserved.
TEST_F(ClangASTGeneratorTest, GenerateTypedef) {
  ClangASTGenerator generator(ast);
  CompilerType alias = builder.CreateTypedefType("MyInt", GetInt());
  clang::QualType qt = generator.Generate(alias);
  auto *tdt = llvm::dyn_cast<clang::TypedefType>(qt.getTypePtr());
  ASSERT_NE(tdt, nullptr);
  EXPECT_EQ(tdt->getDecl()->getNameAsString(), "MyInt");
  EXPECT_EQ(tdt->getDecl()->getUnderlyingType(), ast.IntTy);
}

// An empty (invalid) CompilerType generates a null QualType rather than
// crashing.
TEST_F(ClangASTGeneratorTest, GenerateInvalidTypeIsNull) {
  ClangASTGenerator generator(ast);
  EXPECT_TRUE(generator.Generate(CompilerType()).isNull());
}
