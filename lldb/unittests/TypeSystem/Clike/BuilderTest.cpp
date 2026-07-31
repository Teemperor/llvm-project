//===-- BuilderTest.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeCpp.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

namespace {
struct BuilderTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::Triple("x86_64-pc-linux-gnu"));
};
} // namespace

// Builder::CreateRecordType creates a not-yet-complete record; SetRecordComplete
// (the gated mutator) flips it to complete.
TEST_F(BuilderTest, CreateAndCompleteRecord) {
  Builder builder(*ts);
  CompilerType record =
      builder.CreateRecordType("Foo", 4, /*is_cpp_class=*/false);
  ASSERT_TRUE(record.IsValid());
  auto *r = llvm::cast<RecordType>(
      static_cast<clike_typesystem::Type *>(record.GetOpaqueQualType()));
  EXPECT_FALSE(r->IsComplete());
  builder.SetRecordComplete(*r);
  EXPECT_TRUE(r->IsComplete());
}

// AddField appends a data member with the given name/type/offset.
TEST_F(BuilderTest, AddField) {
  Builder builder(*ts);
  CompilerType int_type = builder.GetBuiltinType(
      "int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  CompilerType record =
      builder.CreateRecordType("Foo", 4, /*is_cpp_class=*/false);
  auto *r = llvm::cast<RecordType>(
      static_cast<clike_typesystem::Type *>(record.GetOpaqueQualType()));

  builder.AddField(*r, builder.GetIdentifier("x"),
                   static_cast<clike_typesystem::Type *>(int_type.GetOpaqueQualType()),
                   /*byte_offset=*/0);
  ASSERT_EQ(r->GetNumFields(), 1u);
  const Field *f = r->GetFieldAtIndex(0);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->name.GetName(), "x");
  EXPECT_EQ(f->byte_offset, 0u);
  EXPECT_EQ(f->type.Get(), int_type.GetOpaqueQualType());
}

// AddBaseClass only works on a ClassType (only those track base classes);
// this exercises the mutator that Context::AddBaseClass gates.
TEST_F(BuilderTest, AddBaseClass) {
  Builder builder(*ts);
  CompilerType base =
      builder.CreateRecordType("Base", 4, /*is_cpp_class=*/true);
  CompilerType derived = builder.CreateRecordType("Derived", 8,
                                                  /*is_cpp_class=*/true);
  auto *derived_class = llvm::cast<ClassType>(
      static_cast<clike_typesystem::Type *>(derived.GetOpaqueQualType()));

  builder.AddBaseClass(*derived_class,
                       static_cast<clike_typesystem::Type *>(base.GetOpaqueQualType()),
                       /*byte_offset=*/0);
  ASSERT_EQ(derived_class->GetNumBaseClasses(), 1u);
  EXPECT_EQ(derived_class->GetBaseClassAtIndex(0)->type.Get(),
           base.GetOpaqueQualType());
}

// AddTemplateArgument records a type-kind template argument (e.g. the `int`
// of `vector<int>`), used by data formatters to recover element types.
TEST_F(BuilderTest, AddTemplateArgument) {
  Builder builder(*ts);
  CompilerType int_type = builder.GetBuiltinType(
      "int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  CompilerType record = builder.CreateRecordType(
      "vector<int>", 24, /*is_cpp_class=*/true);
  auto *r = llvm::cast<ClassType>(
      static_cast<clike_typesystem::Type *>(record.GetOpaqueQualType()));
  builder.SetRecordTemplateInstantiation(*r);

  builder.AddTemplateArgument(*r, lldb::eTemplateArgumentKindType,
                              static_cast<clike_typesystem::Type *>(int_type.GetOpaqueQualType()),
                              /*integral_value=*/0, /*is_default=*/false);
  ASSERT_EQ(r->GetNumTemplateArguments(), 1u);
  const TemplateArgument *arg = r->GetTemplateArgumentAtIndex(0);
  EXPECT_EQ(arg->kind, lldb::eTemplateArgumentKindType);
  EXPECT_EQ(arg->type.Get(), int_type.GetOpaqueQualType());
  EXPECT_TRUE(r->IsTemplateInstantiation());
}

// AddTemplateTemplateArgument records a template-template argument by name
// only (it names another template, which isn't itself a modeled Type).
TEST_F(BuilderTest, AddTemplateTemplateArgument) {
  Builder builder(*ts);
  CompilerType record = builder.CreateRecordType("C<float, T1>",
                                                 std::nullopt, true);
  auto *r = llvm::cast<ClassType>(
      static_cast<clike_typesystem::Type *>(record.GetOpaqueQualType()));

  builder.AddTemplateTemplateArgument(*r, "T1", /*is_default=*/false);
  ASSERT_EQ(r->GetNumTemplateArguments(), 1u);
  const TemplateArgument *arg = r->GetTemplateArgumentAtIndex(0);
  EXPECT_EQ(arg->kind, lldb::eTemplateArgumentKindTemplate);
  EXPECT_EQ(arg->name.GetName(), "T1");
  EXPECT_FALSE(arg->type);
}

// Two Builder-created pointers to the same pointee (through the same
// TypeSystemClike) are the same instance, since pointer types are uniqued at
// the Context level.
TEST_F(BuilderTest, PointerTypesUniquedThroughBuilder) {
  Builder builder(*ts);
  CompilerType record =
      builder.CreateRecordType("Foo", 4, false);
  CompilerType p1 = builder.CreatePointerType(record);
  CompilerType p2 = builder.CreatePointerType(record);
  EXPECT_EQ(p1.GetOpaqueQualType(), p2.GetOpaqueQualType());
}

// SetDeclContext/SetUnqualifiedName record the namespace and unqualified
// spelling used to build a type's (possibly namespace-qualified) display name.
TEST_F(BuilderTest, DeclContextAndUnqualifiedName) {
  Builder builder(*ts);
  const Namespace *ns =
      builder.GetNamespace("std", nullptr, /*is_inline=*/false);
  CompilerType record =
      builder.CreateRecordType("std::string", 32, true);
  builder.SetDeclContext(record, ns);
  builder.SetUnqualifiedName(record, "string");

  auto *t = static_cast<clike_typesystem::Type *>(record.GetOpaqueQualType());
  EXPECT_EQ(t->GetDeclContext(), ns);
  EXPECT_EQ(t->GetUnqualifiedName().GetName(), "string");
}
