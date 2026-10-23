//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Context.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"
#include "Plugins/TypeSystem/Clike/NamedType.h"
#include "Plugins/TypeSystem/Clike/ScopedType.h"

#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike;

namespace {
struct ScopedTypeTest : public testing::Test {
  Context context{llvm::cantFail(
      LanguageOpts::Create(llvm::Triple("x86_64-pc-linux-gnu")))};
  const Namespace *ns =
      context.GetNamespace(context.GetIdentifier("std"), nullptr, false);
};
} // namespace

// ScopedType stores the scope's namespace and still forwards the names to the
// wrapped NamedType.
TEST_F(ScopedTypeTest, StoresDeclContext) {
  ScopedType<NamedType<Type>> type(context.GetIdentifier("std::string"),
                                   DeclScope{ns, context.GetIdentifier("string")});
  EXPECT_EQ(type.GetDeclContext(), ns);
  EXPECT_EQ(type.GetName().GetName(), "std::string");
  EXPECT_EQ(type.GetUnqualifiedName().GetName(), "string");
}

// The C++-capable named kinds (class, enum, typedef) keep their namespace.
TEST_F(ScopedTypeTest, CppKindsKeepDeclContext) {
  RecordType *cls = context.CreateRecordType(
      "std::string", /*byte_size=*/32, /*is_cpp_class=*/true,
      /*is_union=*/false, /*is_class_keyword=*/false,
      DeclScope{ns, context.GetIdentifier("string")});
  EXPECT_EQ(cls->GetDeclContext(), ns);

  Type *int_type = context.GetBuiltinType(BuiltinKind::Int);
  TypedefType *td = context.CreateTypedefType(
      "std::size_type", TypeRef(*int_type),
      DeclScope{ns, context.GetIdentifier("size_type")});
  EXPECT_EQ(td->GetDeclContext(), ns);
}

// C structs and ObjC interfaces don't store a DeclContext: they're always in
// the global namespace.
TEST_F(ScopedTypeTest, CAndObjCKindsAreGlobal) {
  RecordType *s = context.CreateRecordType(
      "Foo", /*byte_size=*/4, /*is_cpp_class=*/false, /*is_union=*/false,
      /*is_class_keyword=*/false,
      DeclScope{nullptr, context.GetIdentifier("Foo")});
  EXPECT_EQ(s->GetDeclContext(), nullptr);
  EXPECT_EQ(s->GetUnqualifiedName().GetName(), "Foo");

  ObjCInterfaceType *iface = context.CreateObjCInterfaceType(
      "NSObject", std::nullopt,
      DeclScope{nullptr, context.GetIdentifier("NSObject")});
  EXPECT_EQ(iface->GetDeclContext(), nullptr);
}
