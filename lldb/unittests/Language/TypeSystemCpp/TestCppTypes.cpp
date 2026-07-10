//===-- TestCppTypes.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Cpp/BuiltinTypes.h"
#include "Plugins/TypeSystem/Cpp/Context.h"
#include "Plugins/TypeSystem/Cpp/LanguageOpts.h"
#include "Plugins/TypeSystem/Cpp/Type.h"

#include "llvm/Support/Casting.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

// The RTTIRoot::isA<>() member should report each node as its own concrete
// kind and as a Type, but not as a sibling kind.
TEST(CppTypesTest, IsAMember) {
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
TEST(CppTypesTest, Isa) {
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
TEST(CppTypesTest, DynCast) {
  StructType record;
  Type *as_type = &record;

  EXPECT_EQ(llvm::dyn_cast<StructType>(as_type), &record);
  EXPECT_EQ(llvm::dyn_cast<BuiltinType>(as_type), nullptr);
  EXPECT_EQ(llvm::dyn_cast<PointerType>(as_type), nullptr);
}

// cast<> performs a checked downcast to the known concrete type.
TEST(CppTypesTest, Cast) {
  BuiltinType builtin;
  Type *as_type = &builtin;

  BuiltinType *casted = llvm::cast<BuiltinType>(as_type);
  EXPECT_EQ(casted, &builtin);
}

// dyn_cast_or_null / cast_or_null handle the null case gracefully.
TEST(CppTypesTest, OrNull) {
  Type *null_type = nullptr;
  EXPECT_EQ(llvm::dyn_cast_or_null<StructType>(null_type), nullptr);
  EXPECT_EQ(llvm::cast_or_null<StructType>(null_type), nullptr);
}

// RTTI must also work for objects created through the Context (the real
// allocation path used by the DWARF parser), not just stack instances.
TEST(CppTypesTest, ContextCreatedTypes) {
  LanguageOpts opts(llvm::Triple("x86_64-pc-linux-gnu"));
  Context context(opts);

  Type *builtin = context.GetBuiltinType("int", /*byte_size=*/4,
                                         lldb::eEncodingSint,
                                         lldb::eFormatDecimal);
  Type *record = context.CreateRecordType("MyStruct", /*byte_size=*/8);

  ASSERT_NE(builtin, nullptr);
  ASSERT_NE(record, nullptr);

  EXPECT_TRUE(llvm::isa<BuiltinType>(builtin));
  EXPECT_FALSE(llvm::isa<StructType>(builtin));

  StructType *as_struct = llvm::dyn_cast<StructType>(record);
  ASSERT_NE(as_struct, nullptr);
  EXPECT_TRUE(as_struct->IsAggregate());
  EXPECT_EQ(as_struct->GetName().GetName(), "MyStruct");
}
