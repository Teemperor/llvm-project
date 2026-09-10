//===-- TypeObjCTest.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/TypeObjC.h"
#include "Plugins/TypeSystem/Clike/Identifier.h"
#include "Plugins/TypeSystem/Clike/Type.h"

#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

namespace {
struct TypeObjCTest : public testing::Test {
  IdentifierMap ids;

  /// A record with \p name, as the debug info spells the runtime's opaque
  /// `objc_object`/`objc_class` records.
  RecordType MakeNamedRecord(llvm::StringRef name) {
    RecordType record;
    record.SetName(ids.get(name));
    return record;
  }
};
} // namespace

// An @interface is an ObjC object type and reuses RecordType's aggregate
// machinery, but reports the ObjC type class rather than a struct's.
TEST_F(TypeObjCTest, InterfaceIsAnObjCAggregate) {
  ObjCInterfaceType interface;
  EXPECT_EQ(interface.GetTypeClass(), lldb::eTypeClassObjCObject);
  EXPECT_EQ(interface.GetTypeInfo(),
            uint32_t(lldb::eTypeHasChildren | lldb::eTypeIsObjC |
                     lldb::eTypeIsStructUnion));
  // Still a record: ivars are modeled as fields.
  EXPECT_TRUE(interface.IsAggregate());
  EXPECT_TRUE(llvm::isa<RecordType>(static_cast<Type *>(&interface)));
  EXPECT_TRUE(llvm::isa<ObjCInterfaceType>(static_cast<Type *>(&interface)));
}

// An interface with no superclass reports no base classes; a plain RecordType
// is not mistaken for an interface.
TEST_F(TypeObjCTest, NoSuperclassMeansNoBaseClass) {
  ObjCInterfaceType root;
  EXPECT_EQ(root.GetNumBaseClasses(), 0u);
  EXPECT_EQ(root.GetBaseClassAtIndex(0), nullptr);

  RecordType plain;
  EXPECT_FALSE(llvm::isa<ObjCInterfaceType>(static_cast<Type *>(&plain)));
}

// Methods are parsed lazily, so a fresh interface reports none.
TEST_F(TypeObjCTest, MethodsStartEmpty) {
  ObjCInterfaceType interface;
  EXPECT_EQ(interface.GetNumObjCMethods(), 0u);
  EXPECT_EQ(interface.GetObjCMethodAtIndex(0), nullptr);
  EXPECT_FALSE(interface.AreMemberFunctionsParsed());
}

// An ObjCMethod requires a type and defaults every other attribute to the
// plain case: an instance method that is neither variadic nor direct and does
// not return instancetype.
TEST_F(TypeObjCTest, MethodDefaults) {
  RecordType fn_type;
  ObjCMethod method{TypeRef(fn_type)};
  EXPECT_EQ(&method.type.Get(), &fn_type);
  EXPECT_TRUE(method.name.GetName().empty());
  EXPECT_TRUE(method.asm_label.GetName().empty());
  EXPECT_FALSE(method.is_class_method);
  EXPECT_FALSE(method.is_variadic);
  EXPECT_FALSE(method.is_direct);
  EXPECT_FALSE(method.returns_instancetype);
}

// The `id`/`Class` idiom: a record named objc_object or objc_class is the
// opaque runtime record those typedefs point at.
TEST_F(TypeObjCTest, OpaqueObjCObjectRecord) {
  RecordType objc_object = MakeNamedRecord("objc_object");
  RecordType objc_class = MakeNamedRecord("objc_class");
  EXPECT_TRUE(IsOpaqueObjCObjectRecord(&objc_object));
  EXPECT_TRUE(IsOpaqueObjCObjectRecord(&objc_class));

  // A differently-named record is not the idiom, nor is a non-record.
  RecordType other = MakeNamedRecord("objc_other");
  EXPECT_FALSE(IsOpaqueObjCObjectRecord(&other));
  EXPECT_FALSE(IsOpaqueObjCObjectRecord(MakeNamedRecord("").Desugar()));

  Type plain;
  EXPECT_FALSE(IsOpaqueObjCObjectRecord(&plain));
  EXPECT_FALSE(IsOpaqueObjCObjectRecord(nullptr));
}

// An @interface is not the opaque-record idiom, even though it is a record.
TEST_F(TypeObjCTest, InterfaceIsNotTheOpaqueRecord) {
  ObjCInterfaceType interface;
  EXPECT_FALSE(IsOpaqueObjCObjectRecord(&interface));
}

// "Is this an ObjC object" covers both spellings: a real @interface and the
// opaque record `id`/`Class` point at.
TEST_F(TypeObjCTest, IsObjCObjectType) {
  ObjCInterfaceType interface;
  RecordType objc_object = MakeNamedRecord("objc_object");
  RecordType plain_record = MakeNamedRecord("MyStruct");
  Type plain;

  EXPECT_TRUE(IsObjCObjectType(&interface));
  EXPECT_TRUE(IsObjCObjectType(&objc_object));
  EXPECT_FALSE(IsObjCObjectType(&plain_record));
  EXPECT_FALSE(IsObjCObjectType(&plain));
  EXPECT_FALSE(IsObjCObjectType(nullptr));
}
