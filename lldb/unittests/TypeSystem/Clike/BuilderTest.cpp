//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeC.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "llvm/Support/Casting.h"
#include "llvm/Support/Error.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike;

namespace {
struct BuilderTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::cantFail(clike::LanguageOpts::Create(
                  llvm::Triple("x86_64-pc-linux-gnu"))));
};
} // namespace

// Builder::CreateRecordType creates a not-yet-complete record;
// SetRecordComplete (the gated mutator) flips it to complete.
TEST_F(BuilderTest, CreateAndCompleteRecord) {
  Builder builder(*ts);
  CompilerType record = builder.CreateRecordType("Foo", 4);
  ASSERT_TRUE(record.IsValid());
  auto *r = llvm::cast<RecordType>(
      static_cast<clike::Type *>(record.GetOpaqueQualType()));
  EXPECT_FALSE(r->IsComplete());
  builder.SetRecordComplete(*r);
  EXPECT_TRUE(r->IsComplete());
}

// A type another Context owns is referenced directly, whether it arrives as a
// CompilerType or as a bare node: a reference names a node, and the node
// records which Context owns it, so nothing has to be interposed to keep that
// recoverable.
TEST_F(BuilderTest, ReferenceToAnotherTypeSystemIsDirect) {
  auto other_ts = std::make_shared<TypeSystemClike>(
      "other", llvm::cantFail(clike::LanguageOpts::Create(
                   llvm::Triple("x86_64-pc-linux-gnu"))));
  CompilerType other_record = Builder(*other_ts).CreateRecordType("Foo", 4);
  auto *other_node =
      static_cast<clike::Type *>(other_record.GetOpaqueQualType());

  Builder builder(*ts);
  auto *pointer = llvm::cast<PointerType>(static_cast<clike::Type *>(
      builder.CreatePointerType(other_record).GetOpaqueQualType()));
  EXPECT_EQ(pointer->GetPointeeType(), other_node);
  // The pointer is ours; the type it points at still belongs to the other one,
  // and says so.
  EXPECT_NE(&pointer->GetOwningContext(), &other_node->GetOwningContext());

  // Same via the raw-node entry points (what the DWARF parser uses).
  auto *record = llvm::cast<RecordType>(static_cast<clike::Type *>(
      builder.CreateRecordType("Bar", 8).GetOpaqueQualType()));
  builder.AddField(*record, builder.GetIdentifier("foo"), other_node,
                   /*byte_offset=*/0);
  ASSERT_EQ(record->GetNumFields(), 1u);
  EXPECT_EQ(&record->GetFieldAtIndex(0)->GetType().Get(), other_node);

  // A type this Builder's own type system owns is referenced the same way.
  CompilerType local_int = builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                                  lldb::eFormatDecimal);
  auto *local_pointer = llvm::cast<PointerType>(static_cast<clike::Type *>(
      builder.CreatePointerType(local_int).GetOpaqueQualType()));
  EXPECT_EQ(local_pointer->GetPointeeType(), local_int.GetOpaqueQualType());
}

// AddField appends a data member with the given name/type/offset.
TEST_F(BuilderTest, AddField) {
  Builder builder(*ts);
  CompilerType int_type = builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                                 lldb::eFormatDecimal);
  CompilerType record = builder.CreateRecordType("Foo", 4);
  auto *r = llvm::cast<RecordType>(
      static_cast<clike::Type *>(record.GetOpaqueQualType()));

  builder.AddField(*r, builder.GetIdentifier("x"),
                   static_cast<clike::Type *>(int_type.GetOpaqueQualType()),
                   /*byte_offset=*/0);
  ASSERT_EQ(r->GetNumFields(), 1u);
  const Field *f = r->GetFieldAtIndex(0);
  ASSERT_NE(f, nullptr);
  EXPECT_EQ(f->GetName().GetName(), "x");
  EXPECT_EQ(f->GetByteOffset(), 0u);
  EXPECT_EQ(&f->GetType().Get(), int_type.GetOpaqueQualType());
}

// A TypeRef always names a type, so the Builder resolves the things debug info
// spells as "no type" into real ones: a missing pointee is `void` and a missing
// enum base is `int`.
TEST_F(BuilderTest, AbsentTypesResolveToRealOnes) {
  Builder builder(*ts);

  // `void *`: DW_TAG_pointer_type with no DW_AT_type.
  auto *void_ptr = llvm::cast<PointerType>(static_cast<clike::Type *>(
      builder.CreatePointerType(CompilerType()).GetOpaqueQualType()));
  EXPECT_TRUE(IsVoid(void_ptr->GetPointeeType()));
  // ... and it is uniqued with a pointer to an explicitly-spelled `void`.
  EXPECT_EQ(
      builder.CreatePointerType(builder.GetVoidType()).GetOpaqueQualType(),
      static_cast<clike::Type *>(void_ptr));

  // An opaque enum records no base type; C says such an enum is an `int`.
  auto *opaque_enum = llvm::cast<EnumType>(static_cast<clike::Type *>(
      builder
          .CreateEnumType("E", /*byte_size=*/std::nullopt, CompilerType(),
                          /*is_scoped=*/false)
          .GetOpaqueQualType()));
  ASSERT_NE(opaque_enum->GetUnderlyingType(), nullptr);
  EXPECT_EQ(opaque_enum->GetUnderlyingType()->GetName().GetName(), "int");
  EXPECT_TRUE(opaque_enum->IsSigned());
  EXPECT_EQ(opaque_enum->GetEncoding(), lldb::eEncodingSint);
}

// A function type always has a return type: nothing-returned is `void`. A
// parameter, by contrast, has no such fallback, so CreateFunctionType reports
// that it could not model one rather than silently shortening the signature.
TEST_F(BuilderTest, FunctionReturnFallsBackToVoidButParametersDoNot) {
  Builder builder(*ts);
  CompilerType fn_type =
      builder.CreateFunctionType(CompilerType(), {}, /*is_variadic=*/false);
  auto *fn = static_cast<clike::Type *>(fn_type.GetOpaqueQualType());
  EXPECT_TRUE(IsVoid(fn->GetReturnType()));
  EXPECT_EQ(fn->GetNumParameters(), 0u);

  CompilerType int_type = builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                                 lldb::eFormatDecimal);
  EXPECT_FALSE(builder.CreateFunctionType(CompilerType(),
                                          {int_type, CompilerType()},
                                          /*is_variadic=*/false));

  CompilerType with_param = builder.CreateFunctionType(
      CompilerType(), {int_type}, /*is_variadic=*/false,
      /*use_void_for_empty_params=*/false, {"x"});
  auto *with_param_fn = llvm::cast<FunctionType>(
      static_cast<clike::Type *>(with_param.GetOpaqueQualType()));
  EXPECT_EQ(with_param_fn->GetNumParameters(), 1u);
  EXPECT_EQ(with_param_fn->GetParameterNameAtIndex(0), "x");
}

// Two Builder-created pointers to the same pointee (through the same
// TypeSystemClike) are the same instance, since pointer types are uniqued at
// the Context level.
TEST_F(BuilderTest, PointerTypesUniquedThroughBuilder) {
  Builder builder(*ts);
  CompilerType record = builder.CreateRecordType("Foo", 4);
  CompilerType p1 = builder.CreatePointerType(record);
  CompilerType p2 = builder.CreatePointerType(record);
  EXPECT_EQ(p1.GetOpaqueQualType(), p2.GetOpaqueQualType());
}

// The DeclScope a named type is created with records the namespace and
// unqualified spelling used to build its (possibly namespace-qualified) display
// name.
TEST_F(BuilderTest, DeclContextAndUnqualifiedName) {
  Builder builder(*ts);
  const Namespace *ns =
      builder.GetNamespace("std", nullptr, /*is_inline=*/false);
  CompilerType record = builder.CreateRecordType(
      "std::string", 32, /*is_union=*/false, /*is_class_keyword=*/false,
      DeclScope{ns, builder.GetIdentifier("string")});

  auto *t = static_cast<clike::Type *>(record.GetOpaqueQualType());
  EXPECT_EQ(t->GetDeclContext(), ns);
  EXPECT_EQ(t->GetUnqualifiedName().GetName(), "string");
}
