//===-- TypeSystemClikeNameTest.cpp ---------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

namespace {
struct TypeSystemClikeNameTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::Triple("x86_64-pc-linux-gnu"));
  Builder builder{*ts};

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }
};
} // namespace

// A plain builtin's type name is just its spelling.
TEST_F(TypeSystemClikeNameTest, BuiltinName) {
  EXPECT_EQ(GetInt().GetTypeName().GetStringRef(), "int");
}

// A pointer/reference has no name of its own; it's built from the pointee's
// name plus the sigil, mirroring C declarator syntax.
TEST_F(TypeSystemClikeNameTest, PointerAndReferenceNames) {
  CompilerType ptr = builder.CreatePointerType(GetInt());
  EXPECT_EQ(ptr.GetTypeName().GetStringRef(), "int *");

  CompilerType ptr_ptr = builder.CreatePointerType(ptr);
  // No space between two pointer sigils.
  EXPECT_EQ(ptr_ptr.GetTypeName().GetStringRef(), "int **");

  CompilerType lref = builder.CreateReferenceType(GetInt(), false);
  EXPECT_EQ(lref.GetTypeName().GetStringRef(), "int &");

  CompilerType rref = builder.CreateReferenceType(GetInt(), true);
  EXPECT_EQ(rref.GetTypeName().GetStringRef(), "int &&");
}

// An array's name is "<element>[<count>]"; an array of unknown bound omits
// the count.
TEST_F(TypeSystemClikeNameTest, ArrayNames) {
  CompilerType bounded = builder.CreateArrayType(GetInt(), 4);
  EXPECT_EQ(bounded.GetTypeName().GetStringRef(), "int[4]");

  CompilerType unbounded = builder.CreateArrayType(GetInt(), std::nullopt);
  EXPECT_EQ(unbounded.GetTypeName().GetStringRef(), "int[]");
}

// A cv-qualified builtin spells its qualifiers as a prefix; a cv-qualified
// record/enum/typedef drops them (their name comes from the decl, matching
// TypeSystemClang).
TEST_F(TypeSystemClikeNameTest, CVQualifiedNames) {
  CompilerType const_int =
      builder.CreateCVQualifiedType(GetInt(), /*is_const=*/true,
                                    /*is_volatile=*/false);
  EXPECT_EQ(const_int.GetTypeName().GetStringRef(), "const int");

  CompilerType volatile_int =
      builder.CreateCVQualifiedType(GetInt(), false, true);
  EXPECT_EQ(volatile_int.GetTypeName().GetStringRef(), "volatile int");

  CompilerType record = builder.CreateRecordType("Foo", 4, false);
  CompilerType const_record =
      builder.CreateCVQualifiedType(record, true, false);
  EXPECT_EQ(const_record.GetTypeName().GetStringRef(), "Foo");
}

// A named leaf type (record, typedef) reports its stored spelling verbatim.
TEST_F(TypeSystemClikeNameTest, NamedLeafTypes) {
  CompilerType record = builder.CreateRecordType("MyStruct", 4, false);
  EXPECT_EQ(record.GetTypeName().GetStringRef(), "MyStruct");

  CompilerType alias = builder.CreateTypedefType("MyAlias", GetInt());
  EXPECT_EQ(alias.GetTypeName().GetStringRef(), "MyAlias");
}

// An unnamed struct/class/union prints as "(unnamed struct)" etc, matching
// clang's anonymous-tag spelling.
TEST_F(TypeSystemClikeNameTest, UnnamedRecordName) {
  CompilerType anon_struct =
      builder.CreateRecordType("", 4, /*is_cpp_class=*/false);
  EXPECT_EQ(anon_struct.GetTypeName().GetStringRef(), "(unnamed struct)");

  CompilerType anon_union = builder.CreateRecordType(
      "", 4, /*is_cpp_class=*/false, /*is_union=*/true);
  EXPECT_EQ(anon_union.GetTypeName().GetStringRef(), "(unnamed union)");
}

// GetDisplayTypeName prefixes a type's declaring (non-inline) namespaces, but
// skips an inline namespace (e.g. libc++'s `std::__1`) -- so `std::__1::foo`
// still displays as `std::foo`.
TEST_F(TypeSystemClikeNameTest, DisplayNameSkipsInlineNamespace) {
  const Namespace *std_ns =
      builder.GetNamespace("std", nullptr, /*is_inline=*/false);
  const Namespace *inline_ns = builder.GetNamespace(
      "__1", std_ns, /*is_inline=*/true);

  CompilerType record =
      builder.CreateRecordType("std::__1::basic_string", 32, true);
  builder.SetDeclContext(record, inline_ns);
  builder.SetUnqualifiedName(record, "basic_string");

  EXPECT_EQ(ts->GetDisplayTypeName(record.GetOpaqueQualType()).GetStringRef(),
           "std::basic_string");
}

// A class-template instantiation's display name hides defaulted template
// arguments (e.g. `vector<int>` instead of `vector<int, allocator<int>>`),
// while GetTypeName (the fully-qualified/DWARF-matching name) keeps them.
TEST_F(TypeSystemClikeNameTest, TemplateDisplayNameHidesDefaultArgs) {
  CompilerType record = builder.CreateRecordType(
      "vector<int, allocator<int> >", 24, true);
  auto *r =
      llvm::cast<ClassType>(static_cast<clike_typesystem::Type *>(record.GetOpaqueQualType()));
  builder.SetRecordTemplateInstantiation(*r);
  builder.SetUnqualifiedName(record, "vector<int, allocator<int> >");

  CompilerType allocator =
      builder.CreateRecordType("allocator<int>", 1, true);

  builder.AddTemplateArgument(*r, lldb::eTemplateArgumentKindType,
                              static_cast<clike_typesystem::Type *>(GetInt().GetOpaqueQualType()),
                              0, /*is_default=*/false);
  builder.AddTemplateArgument(
      *r, lldb::eTemplateArgumentKindType,
      static_cast<clike_typesystem::Type *>(allocator.GetOpaqueQualType()), 0,
      /*is_default=*/true);

  EXPECT_EQ(ts->GetDisplayTypeName(record.GetOpaqueQualType()).GetStringRef(),
           "vector<int>");
}
