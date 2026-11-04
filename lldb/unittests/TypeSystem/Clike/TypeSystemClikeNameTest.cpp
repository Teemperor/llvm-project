//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike;

namespace {
struct TypeSystemClikeNameTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::cantFail(clike::LanguageOpts::Create(
                  llvm::Triple("x86_64-pc-linux-gnu"))));
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

// A pointer has no name of its own; it's built from the pointee's name plus
// the sigil, mirroring C declarator syntax.
TEST_F(TypeSystemClikeNameTest, PointerNames) {
  CompilerType ptr = builder.CreatePointerType(GetInt());
  EXPECT_EQ(ptr.GetTypeName().GetStringRef(), "int *");

  CompilerType ptr_ptr = builder.CreatePointerType(ptr);
  // No space between two pointer sigils.
  EXPECT_EQ(ptr_ptr.GetTypeName().GetStringRef(), "int **");
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

  CompilerType record = builder.CreateRecordType("Foo", 4);
  CompilerType const_record =
      builder.CreateCVQualifiedType(record, true, false);
  EXPECT_EQ(const_record.GetTypeName().GetStringRef(), "Foo");
}

// A named leaf type (record, typedef) reports its stored spelling verbatim.
TEST_F(TypeSystemClikeNameTest, NamedLeafTypes) {
  CompilerType record = builder.CreateRecordType("MyStruct", 4);
  EXPECT_EQ(record.GetTypeName().GetStringRef(), "MyStruct");

  CompilerType alias = builder.CreateTypedefType("MyAlias", GetInt());
  EXPECT_EQ(alias.GetTypeName().GetStringRef(), "MyAlias");
}

// An unnamed struct/class/union prints as "(unnamed struct)" etc, matching
// clang's anonymous-tag spelling.
TEST_F(TypeSystemClikeNameTest, UnnamedRecordName) {
  CompilerType anon_struct = builder.CreateRecordType("", 4);
  EXPECT_EQ(anon_struct.GetTypeName().GetStringRef(), "(unnamed struct)");

  CompilerType anon_union = builder.CreateRecordType("", 4, /*is_union=*/true);
  EXPECT_EQ(anon_union.GetTypeName().GetStringRef(), "(unnamed union)");
}

// GetDisplayTypeName prefixes a type's declaring (non-inline) namespaces, but
// skips an inline namespace (e.g. libc++'s `std::__1`) -- so `std::__1::foo`
// still displays as `std::foo`.
TEST_F(TypeSystemClikeNameTest, DisplayNameSkipsInlineNamespace) {
  const Namespace *std_ns =
      builder.GetNamespace("std", nullptr, /*is_inline=*/false);
  const Namespace *inline_ns =
      builder.GetNamespace("__1", std_ns, /*is_inline=*/true);

  CompilerType record =
      builder.CreateRecordType("std::__1::basic_string", 32, /*is_union=*/false,
                               /*is_class_keyword=*/false, inline_ns,
                               builder.GetIdentifier("basic_string"));

  EXPECT_EQ(ts->GetDisplayTypeName(record.GetOpaqueQualType()).GetStringRef(),
            "std::basic_string");
}

// A CompilerDeclContext wraps a declaration context. Its qualified name skips
// inline namespaces, while its CompilerContext chain lists every scope.
TEST_F(TypeSystemClikeNameTest, CompilerDeclContext) {
  const Namespace *std_ns =
      builder.GetNamespace("std", nullptr, /*is_inline=*/false);
  const Namespace *inline_ns =
      builder.GetNamespace("__1", std_ns, /*is_inline=*/true);
  CompilerType record = builder.CreateRecordType(
      "std::__1::Outer", 1, /*is_union=*/false, /*is_class_keyword=*/false,
      inline_ns, builder.GetIdentifier("Outer"));
  const auto *outer = llvm::cast<RecordType>(
      static_cast<clike::Type *>(record.GetOpaqueQualType()));

  EXPECT_FALSE(ts->GetCompilerDeclContext(nullptr).IsValid());

  CompilerDeclContext ns_ctx = ts->GetCompilerDeclContext(inline_ns);
  EXPECT_EQ(ns_ctx.GetName().GetStringRef(), "__1");
  EXPECT_EQ(ns_ctx.GetScopeQualifiedName().GetStringRef(), "std");
  EXPECT_EQ(TypeSystemClike::GetDeclarationContext(
                ns_ctx.GetOpaqueDeclContext()),
            inline_ns);

  CompilerDeclContext record_ctx = ts->GetCompilerDeclContext(outer);
  EXPECT_EQ(record_ctx.GetName().GetStringRef(), "Outer");
  EXPECT_EQ(record_ctx.GetScopeQualifiedName().GetStringRef(), "std::Outer");
  EXPECT_EQ(TypeSystemClike::GetDeclarationContext(
                record_ctx.GetOpaqueDeclContext()),
            outer);
  std::vector<CompilerContext> context = record_ctx.GetCompilerContext();
  ASSERT_EQ(context.size(), 3u);
  EXPECT_EQ(context[0].kind, CompilerContextKind::Namespace);
  EXPECT_EQ(context[0].name.GetStringRef(), "std");
  EXPECT_EQ(context[1].kind, CompilerContextKind::Namespace);
  EXPECT_EQ(context[1].name.GetStringRef(), "__1");
  EXPECT_EQ(context[2].kind, CompilerContextKind::ClassOrStruct);
  EXPECT_EQ(context[2].name.GetStringRef(), "Outer");

  // The lookup of `std` also contains the inline namespace, but not the record
  // declared in it.
  CompilerDeclContext std_ctx = ts->GetCompilerDeclContext(std_ns);
  EXPECT_TRUE(std_ctx.IsContainedInLookup(ns_ctx));
  EXPECT_FALSE(std_ctx.IsContainedInLookup(record_ctx));
}
