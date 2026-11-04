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
