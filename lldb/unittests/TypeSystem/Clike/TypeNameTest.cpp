//===-- TypeNameTest.cpp --------------------------------------------------===//
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
#include "Plugins/TypeSystem/Clike/TypeCpp.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

namespace {
struct TypeNameTest : public testing::Test {
  Context context{
      llvm::cantFail(LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};

  Type *Int() { return context.GetBuiltinType(BuiltinKind::Int); }
  Type *Char() { return context.GetBuiltinType(BuiltinKind::Char); }
  Type *Void() { return context.GetBuiltinType(BuiltinKind::Void); }

  RecordType *Record(llvm::StringRef name, bool is_cpp_class = false) {
    RecordType *record = context.CreateRecordType(name, /*byte_size=*/8,
                                                  is_cpp_class);
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
  EXPECT_EQ(Display(Record("MyClass", /*is_cpp_class=*/true)), "MyClass");
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

// An lvalue reference renders with one `&`, an rvalue one with two.
TEST_F(TypeNameTest, Reference) {
  EXPECT_EQ(Canonical(context.CreateReferenceType(TypeRef(*Int()),
                                                  /*is_rvalue=*/false)),
            "int &");
  EXPECT_EQ(Canonical(context.CreateReferenceType(TypeRef(*Int()),
                                                  /*is_rvalue=*/true)),
            "int &&");
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
  RecordType *record = Record("ns::Foo", /*is_cpp_class=*/true);
  record->SetDeclContext(ns);
  record->SetUnqualifiedName(context.GetIdentifier("Foo"));

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

  RecordType *record = Record("std::__1::string", /*is_cpp_class=*/true);
  record->SetDeclContext(inline_ns);
  record->SetUnqualifiedName(context.GetIdentifier("string"));

  EXPECT_EQ(Display(record), "std::string");
  // Keeping inline namespaces spells the full scope.
  EXPECT_EQ(BuildDisplayName(record, /*hide_default_args=*/true,
                             /*keep_inline_namespaces=*/true),
            "std::__1::string");
}

// A function type renders in C declarator form, and BuildFunctionName places
// the caller's declarator between the return type and the parameter list.
TEST_F(TypeNameTest, FunctionDeclarator) {
  FunctionType *fn =
      context.CreateFunctionType(TypeRef(*Int()), /*is_variadic=*/false);
  CVQualifiedType *const_char =
      context.CreateCVQualifiedType(TypeRef(*Char()), /*is_const=*/true,
                                    /*is_volatile=*/false);
  context.AddParameter(*fn, TypeRef(*context.CreatePointerType(
                                 TypeRef(*const_char))));

  EXPECT_EQ(BuildFunctionName(fn, ""), "int (const char *)");
  EXPECT_EQ(BuildFunctionName(fn, "(*)"), "int (*)(const char *)");
  EXPECT_EQ(BuildFunctionName(fn, "(&)"), "int (&)(const char *)");
}

// A variadic function's parameter list ends in `...`; an empty one renders as
// `()` unless the type asked for the `(void)` spelling.
TEST_F(TypeNameTest, FunctionVariadicAndEmptyParams) {
  FunctionType *variadic =
      context.CreateFunctionType(TypeRef(*Int()), /*is_variadic=*/true);
  context.AddParameter(*variadic, TypeRef(*Int()));
  EXPECT_EQ(BuildFunctionName(variadic, ""), "int (int, ...)");

  FunctionType *no_params =
      context.CreateFunctionType(TypeRef(*Void()), /*is_variadic=*/false);
  EXPECT_EQ(BuildFunctionName(no_params, ""), "void ()");

  FunctionType *void_params = context.CreateFunctionType(
      TypeRef(*Void()), /*is_variadic=*/false,
      /*use_void_for_empty_params=*/true);
  EXPECT_EQ(BuildFunctionName(void_params, ""), "void (void)");
}

// A function pointer renders as a declarator around the pointer, not as
// `int ()*`.
TEST_F(TypeNameTest, FunctionPointerName) {
  FunctionType *fn =
      context.CreateFunctionType(TypeRef(*Int()), /*is_variadic=*/false);
  context.AddParameter(*fn, TypeRef(*Int()));
  PointerType *fn_ptr = context.CreatePointerType(TypeRef(*fn));
  EXPECT_EQ(Canonical(fn_ptr), "int (*)(int)");
}

// A defaulted template argument is kept in the canonical name and dropped from
// the display name.
TEST_F(TypeNameTest, DefaultedTemplateArgumentHiddenInDisplayName) {
  ClassType *record = llvm::cast<ClassType>(
      context.CreateRecordType("Vec<int, Alloc<int> >", /*byte_size=*/8,
                               /*is_cpp_class=*/true));
  context.SetComplete(*record);
  context.SetTemplateInstantiation(*record);
  record->SetUnqualifiedName(context.GetIdentifier("Vec"));

  TemplateArgument arg;
  arg.kind = lldb::eTemplateArgumentKindType;
  arg.type = TypeRef(*Int());
  context.AddTemplateArgument(*record, arg);

  TemplateArgument defaulted;
  defaulted.kind = lldb::eTemplateArgumentKindType;
  defaulted.type = TypeRef(*Record("Alloc<int>", /*is_cpp_class=*/true));
  defaulted.is_default = true;
  context.AddTemplateArgument(*record, defaulted);

  // The canonical name is the raw DWARF spelling, defaults and all.
  EXPECT_EQ(Canonical(record), "Vec<int, Alloc<int> >");
  // The display name re-renders from the modeled arguments, dropping defaults.
  EXPECT_EQ(Display(record), "Vec<int>");
  // ...and keeps them when asked to.
  EXPECT_EQ(BuildDisplayName(record, /*hide_default_args=*/false),
            "Vec<int, Alloc<int> >");
}

// A variadic template specialized over an empty pack still prints an empty
// argument list, which is why "is a template instantiation" is tracked
// separately from "has arguments".
TEST_F(TypeNameTest, EmptyTemplateArgumentList) {
  ClassType *record = llvm::cast<ClassType>(context.CreateRecordType(
      "Pack<>", /*byte_size=*/8, /*is_cpp_class=*/true));
  context.SetComplete(*record);
  context.SetTemplateInstantiation(*record);
  record->SetUnqualifiedName(context.GetIdentifier("Pack"));

  EXPECT_EQ(record->GetNumTemplateArguments(), 0u);
  EXPECT_TRUE(record->IsTemplateInstantiation());
  EXPECT_EQ(Display(record), "Pack<>");
}
