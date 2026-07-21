//===-- ClangASTGeneratorVBaseTest.cpp ----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangASTGeneratorTestUtils.h"

#include "Plugins/TypeSystem/Cpp/Type.h"

#include "llvm/Support/Casting.h"

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

namespace {
using ClangASTGeneratorVBaseTest = ClangASTGeneratorTestUtils;

ClassType *AsClass(const CompilerType &ct) {
  return llvm::cast<ClassType>(
      static_cast<cpp_typesystem::Type *>(ct.GetOpaqueQualType()));
}
} // namespace

// A virtual base has no constant offset in the object; TypeSystemCpp normally
// reads the Itanium vtable-relative offset-offset from DWARF. When that DWARF
// expression is missing (dsymutil strips it from a .dSYM),
// ClangASTGenerator::ComputeVBaseOffsetOffset recomputes it from a synthesized
// Clang vtable layout. Build a `struct B : virtual A` diamond leg and check the
// recomputed offset-offset is a plausible (positive, word-aligned) value.
TEST_F(ClangASTGeneratorVBaseTest, ComputeVBaseOffsetOffset) {
  // struct A { int a; };  -- the (polymorphic, because it is a virtual base)
  // shared base.
  CompilerType a = builder.CreateRecordType("A", 8, /*is_cpp_class=*/true);
  ClassType *a_cls = AsClass(a);
  builder.AddField(*a_cls, builder.GetIdentifier("a"),
                   static_cast<cpp_typesystem::Type *>(GetInt().GetOpaqueQualType()),
                   0);
  builder.SetRecordPolymorphic(*a_cls);
  builder.SetRecordComplete(*a_cls);

  // struct B : virtual A { int b; };
  CompilerType b = builder.CreateRecordType("B", 24, /*is_cpp_class=*/true);
  ClassType *b_cls = AsClass(b);
  builder.AddBaseClass(
      *b_cls, static_cast<cpp_typesystem::Type *>(a.GetOpaqueQualType()),
      /*byte_offset=*/0, /*is_virtual=*/true);
  builder.AddField(*b_cls, builder.GetIdentifier("b"),
                   static_cast<cpp_typesystem::Type *>(GetInt().GetOpaqueQualType()),
                   8);
  builder.SetRecordPolymorphic(*b_cls);
  builder.SetRecordComplete(*b_cls);

  std::optional<uint64_t> ooo = ClangASTGenerator::ComputeVBaseOffsetOffset(
      *ts, ts->GetTriple(), b, a);
  ASSERT_TRUE(ooo.has_value());
  // It is a positive, pointer-word-aligned byte value (subtracted from the
  // vtable pointer by ReadVirtualBaseOffset).
  EXPECT_GT(*ooo, 0u);
  EXPECT_EQ(*ooo % 8, 0u);
}

// A non-virtual base has a constant offset and no vtable-relative offset-offset,
// so ComputeVBaseOffsetOffset must not invent one (querying it is meaningless).
TEST_F(ClangASTGeneratorVBaseTest, NonVirtualBaseHasNoOffsetOffset) {
  CompilerType a = builder.CreateRecordType("A2", 4, /*is_cpp_class=*/true);
  ClassType *a_cls = AsClass(a);
  builder.AddField(*a_cls, builder.GetIdentifier("a"),
                   static_cast<cpp_typesystem::Type *>(GetInt().GetOpaqueQualType()),
                   0);
  builder.SetRecordComplete(*a_cls);

  CompilerType b = builder.CreateRecordType("B2", 8, /*is_cpp_class=*/true);
  ClassType *b_cls = AsClass(b);
  builder.AddBaseClass(
      *b_cls, static_cast<cpp_typesystem::Type *>(a.GetOpaqueQualType()),
      /*byte_offset=*/0, /*is_virtual=*/false);
  builder.AddField(*b_cls, builder.GetIdentifier("b"),
                   static_cast<cpp_typesystem::Type *>(GetInt().GetOpaqueQualType()),
                   4);
  builder.SetRecordComplete(*b_cls);

  // B2 is not polymorphic and A2 is not a virtual base, so there is no vtable
  // slot for it.
  std::optional<uint64_t> ooo = ClangASTGenerator::ComputeVBaseOffsetOffset(
      *ts, ts->GetTriple(), b, a);
  EXPECT_FALSE(ooo.has_value());
}
