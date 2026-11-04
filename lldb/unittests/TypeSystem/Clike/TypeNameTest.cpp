//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/TypeName.h"
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
struct TypeNameTest : public testing::Test {
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};

  Type *Int() { return context.GetBuiltinType(BuiltinKind::Int); }
  Type *Char() { return context.GetBuiltinType(BuiltinKind::Char); }
  Type *Void() { return context.GetBuiltinType(BuiltinKind::Void); }

  RecordType *Record(llvm::StringRef name, DeclScope scope = {}) {
    RecordType *record =
        context.CreateRecordType(name, /*byte_size=*/8, /*is_union=*/false,
                                 /*is_class_keyword=*/false, scope);
    context.SetComplete(*record);
    return record;
  }

  std::string Canonical(Type *t) {
    return BuildCanonicalName(t, /*base_only=*/false);
  }
  std::string Display(Type *t) { return BuildDisplayName(t); }
};
} // namespace

// A builtin renders as its plain spelling.
TEST_F(TypeNameTest, Builtin) {
  EXPECT_EQ(Canonical(Int()), "int");
  EXPECT_EQ(Canonical(Void()), "void");
  EXPECT_EQ(Display(Int()), "int");
}

// A record renders with the name the debug info recorded.
TEST_F(TypeNameTest, Record) {
  EXPECT_EQ(Canonical(Record("MyStruct")), "MyStruct");
  EXPECT_EQ(Display(Record("MyStruct")), "MyStruct");
}

// A pointer is spelled in C declarator form, and pointer-to-pointer nests
// without inserting spaces between the stars.
TEST_F(TypeNameTest, Pointer) {
  PointerType *int_ptr = context.CreatePointerType(TypeRef(*Int()));
  EXPECT_EQ(Canonical(int_ptr), "int *");

  PointerType *int_ptr_ptr = context.CreatePointerType(TypeRef(*int_ptr));
  EXPECT_EQ(Canonical(int_ptr_ptr), "int **");

  EXPECT_EQ(Canonical(context.CreatePointerType(TypeRef(*Void()))), "void *");
}

// A qualifier is spelled before the type it applies to, and `const char *`
// keeps the qualifier on the pointee rather than on the pointer.
TEST_F(TypeNameTest, CVQualified) {
  CVQualifiedType *const_int =
      context.CreateCVQualifiedType(TypeRef(*Int()), /*is_const=*/true,
                                    /*is_volatile=*/false);
  EXPECT_EQ(Canonical(const_int), "const int");

  CVQualifiedType *const_char =
      context.CreateCVQualifiedType(TypeRef(*Char()), /*is_const=*/true,
                                    /*is_volatile=*/false);
  EXPECT_EQ(Canonical(context.CreatePointerType(TypeRef(*const_char))),
            "const char *");

  CVQualifiedType *volatile_int =
      context.CreateCVQualifiedType(TypeRef(*Int()), /*is_const=*/false,
                                    /*is_volatile=*/true);
  EXPECT_EQ(Canonical(volatile_int), "volatile int");
}

// An array renders with its bound; an unbounded one with empty brackets.
TEST_F(TypeNameTest, Array) {
  EXPECT_EQ(Canonical(context.CreateArrayType(TypeRef(*Int()), 4)), "int[4]");
  EXPECT_EQ(Canonical(context.CreateArrayType(TypeRef(*Int()), std::nullopt)),
            "int[]");
}

// A typedef reports its own name rather than what it aliases.
TEST_F(TypeNameTest, Typedef) {
  TypedefType *alias = context.CreateTypedefType("my_int", TypeRef(*Int()));
  EXPECT_EQ(Canonical(alias), "my_int");
  EXPECT_EQ(Display(alias), "my_int");
}

// An enum reports its own name.
TEST_F(TypeNameTest, Enum) {
  EnumType *e = context.CreateEnumType("Color", /*byte_size=*/4,
                                       TypeRef(*Int()), /*is_scoped=*/false);
  EXPECT_EQ(Canonical(e), "Color");
}

// A namespace-qualified type renders with its scope; base_only asks for the
// unqualified spelling instead.
TEST_F(TypeNameTest, NamespaceQualified) {
  const Namespace *ns = context.GetNamespace(context.GetIdentifier("ns"),
                                             nullptr, /*is_inline=*/false);
  RecordType *record =
      Record("ns::Foo", DeclScope{ns, context.GetIdentifier("Foo")});

  EXPECT_EQ(BuildCanonicalName(record, /*base_only=*/false), "ns::Foo");
  EXPECT_EQ(BuildCanonicalName(record, /*base_only=*/true), "Foo");
}

// An inline namespace is transparent in the display name (libc++'s `std::__1`
// prints as `std`) but is kept in the canonical one.
TEST_F(TypeNameTest, InlineNamespaceHiddenInDisplayName) {
  const Namespace *std_ns = context.GetNamespace(context.GetIdentifier("std"),
                                                 nullptr, /*is_inline=*/false);
  const Namespace *inline_ns = context.GetNamespace(
      context.GetIdentifier("__1"), std_ns, /*is_inline=*/true);

  RecordType *record =
      Record("std::__1::string",
             DeclScope{inline_ns, context.GetIdentifier("string")});

  EXPECT_EQ(Display(record), "std::string");
  // Keeping inline namespaces spells the full scope.
  EXPECT_EQ(BuildDisplayName(record, /*keep_inline_namespaces=*/true),
            "std::__1::string");
}

// A function type renders in C declarator form, and BuildFunctionName places
// the caller's declarator between the return type and the parameter list.
TEST_F(TypeNameTest, FunctionDeclarator) {
  CVQualifiedType *const_char =
      context.CreateCVQualifiedType(TypeRef(*Char()), /*is_const=*/true,
                                    /*is_volatile=*/false);
  FunctionType *fn = context.CreateFunctionType(
      TypeRef(*Int()),
      {TypeRef(*context.CreatePointerType(TypeRef(*const_char)))},
      /*is_variadic=*/false);

  EXPECT_EQ(BuildFunctionName(fn, ""), "int (const char *)");
  EXPECT_EQ(BuildFunctionName(fn, "(*)"), "int (*)(const char *)");
  EXPECT_EQ(BuildFunctionName(fn, "(^)"), "int (^)(const char *)");
}

// A variadic function's parameter list ends in `...`; an empty one renders as
// `()` unless the type asked for the `(void)` spelling.
TEST_F(TypeNameTest, FunctionVariadicAndEmptyParams) {
  FunctionType *variadic = context.CreateFunctionType(
      TypeRef(*Int()), {TypeRef(*Int())}, /*is_variadic=*/true);
  EXPECT_EQ(BuildFunctionName(variadic, ""), "int (int, ...)");

  FunctionType *no_params =
      context.CreateFunctionType(TypeRef(*Void()), {}, /*is_variadic=*/false);
  EXPECT_EQ(BuildFunctionName(no_params, ""), "void ()");

  FunctionType *void_params =
      context.CreateFunctionType(TypeRef(*Void()), {}, /*is_variadic=*/false,
                                 /*use_void_for_empty_params=*/true);
  EXPECT_EQ(BuildFunctionName(void_params, ""), "void (void)");
}

// A function pointer renders as a declarator around the pointer, not as
// `int ()*`.
TEST_F(TypeNameTest, FunctionPointerName) {
  FunctionType *fn = context.CreateFunctionType(
      TypeRef(*Int()), {TypeRef(*Int())}, /*is_variadic=*/false);
  PointerType *fn_ptr = context.CreatePointerType(TypeRef(*fn));
  EXPECT_EQ(Canonical(fn_ptr), "int (*)(int)");
}
