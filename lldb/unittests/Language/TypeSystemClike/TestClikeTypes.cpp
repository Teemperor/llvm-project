//===-- TestClikeTypes.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/BuiltinTypes.h"
#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"
#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeC.h"
#include "Plugins/TypeSystem/Clike/TypeCpp.h"

#include "llvm/Support/Casting.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

// The RTTIRoot::isA<>() member should report each node as its own concrete
// kind and as a Type, but not as a sibling kind.
TEST(ClikeTypesTest, IsAMember) {
  BuiltinType builtin;
  StructType record;
  PointerType pointer;

  EXPECT_TRUE(builtin.isA<Type>());
  EXPECT_TRUE(record.isA<Type>());
  EXPECT_TRUE(pointer.isA<Type>());

  EXPECT_TRUE(builtin.isA<BuiltinType>());
  EXPECT_FALSE(builtin.isA<StructType>());
  EXPECT_FALSE(builtin.isA<PointerType>());

  EXPECT_TRUE(record.isA<StructType>());
  EXPECT_FALSE(record.isA<BuiltinType>());

  EXPECT_TRUE(pointer.isA<PointerType>());
  EXPECT_FALSE(pointer.isA<StructType>());
}

// llvm::isa<> must recognize the dynamic type behind a Type* base pointer and
// reject the wrong kinds.
TEST(ClikeTypesTest, Isa) {
  BuiltinType builtin;
  StructType record;
  PointerType pointer;

  Type *as_builtin = &builtin;
  Type *as_record = &record;
  Type *as_pointer = &pointer;

  EXPECT_TRUE(llvm::isa<BuiltinType>(as_builtin));
  EXPECT_FALSE(llvm::isa<StructType>(as_builtin));
  EXPECT_FALSE(llvm::isa<PointerType>(as_builtin));

  EXPECT_TRUE(llvm::isa<StructType>(as_record));
  EXPECT_FALSE(llvm::isa<BuiltinType>(as_record));
  EXPECT_FALSE(llvm::isa<PointerType>(as_record));

  EXPECT_TRUE(llvm::isa<PointerType>(as_pointer));
  EXPECT_FALSE(llvm::isa<BuiltinType>(as_pointer));
  EXPECT_FALSE(llvm::isa<StructType>(as_pointer));
}

// dyn_cast<> returns the derived pointer on a match and null otherwise.
TEST(ClikeTypesTest, DynCast) {
  StructType record;
  Type *as_type = &record;

  EXPECT_EQ(llvm::dyn_cast<StructType>(as_type), &record);
  EXPECT_EQ(llvm::dyn_cast<BuiltinType>(as_type), nullptr);
  EXPECT_EQ(llvm::dyn_cast<PointerType>(as_type), nullptr);
}

// cast<> performs a checked downcast to the known concrete type.
TEST(ClikeTypesTest, Cast) {
  BuiltinType builtin;
  Type *as_type = &builtin;

  BuiltinType *casted = llvm::cast<BuiltinType>(as_type);
  EXPECT_EQ(casted, &builtin);
}

// dyn_cast_or_null / cast_or_null handle the null case gracefully.
TEST(ClikeTypesTest, OrNull) {
  Type *null_type = nullptr;
  EXPECT_EQ(llvm::dyn_cast_or_null<StructType>(null_type), nullptr);
  EXPECT_EQ(llvm::cast_or_null<StructType>(null_type), nullptr);
}

// RTTI must also work for objects created through the Context (the real
// allocation path used by the DWARF parser), not just stack instances.
TEST(ClikeTypesTest, ContextCreatedTypes) {
  LanguageOpts opts(llvm::Triple("x86_64-pc-linux-gnu"));
  Context context(opts);

  Type *builtin = context.GetBuiltinType("int", /*byte_size=*/4,
                                         lldb::eEncodingSint,
                                         lldb::eFormatDecimal);
  Type *record = context.CreateRecordType("MyStruct", /*byte_size=*/8,
                                          /*is_cpp_class=*/false);

  ASSERT_NE(builtin, nullptr);
  ASSERT_NE(record, nullptr);

  EXPECT_TRUE(llvm::isa<BuiltinType>(builtin));
  EXPECT_FALSE(llvm::isa<StructType>(builtin));

  StructType *as_struct = llvm::dyn_cast<StructType>(record);
  ASSERT_NE(as_struct, nullptr);
  EXPECT_TRUE(as_struct->IsAggregate());
  EXPECT_EQ(as_struct->GetName().GetName(), "MyStruct");
  // A plain C struct is a RecordType but never a ClassType.
  EXPECT_TRUE(llvm::isa<RecordType>(record));
  EXPECT_FALSE(llvm::isa<ClassType>(record));
}

// A C++ record is created as a ClassType that can carry base classes, while
// still being a RecordType (and thus aggregate) like a plain struct.
TEST(ClikeTypesTest, ClassTypeBaseClasses) {
  LanguageOpts opts(llvm::Triple("x86_64-pc-linux-gnu"));
  Context context(opts);

  Type *base = context.CreateRecordType("Base", /*byte_size=*/8,
                                        /*is_cpp_class=*/true);
  Type *derived = context.CreateRecordType("Derived", /*byte_size=*/16,
                                            /*is_cpp_class=*/true);

  ASSERT_NE(base, nullptr);
  ASSERT_NE(derived, nullptr);

  EXPECT_TRUE(llvm::isa<RecordType>(derived));
  ClassType *as_class = llvm::dyn_cast<ClassType>(derived);
  ASSERT_NE(as_class, nullptr);
  EXPECT_FALSE(llvm::isa<StructType>(derived));

  EXPECT_EQ(as_class->GetNumBaseClasses(), 0u);
  // Base classes are added through the Context (the gated mutation entry
  // point); ClassType::AddBaseClass itself is private.
  context.AddBaseClass(*as_class, TypeRef(context, base), /*byte_offset=*/0);
  ASSERT_EQ(as_class->GetNumBaseClasses(), 1u);
  const BaseClass *b = as_class->GetBaseClassAtIndex(0);
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->type.Get(), base);
  EXPECT_EQ(b->byte_offset, 0u);
}
