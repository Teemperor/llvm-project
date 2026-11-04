//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "llvm/Support/Error.h"
#include "llvm/Testing/Support/Error.h"

#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike;

namespace {
struct TypeSystemClikeChildrenTest : public testing::Test {
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

// A pointer to a scalar (non-aggregate) always has exactly one (deref) child,
// regardless of the transparent_pointers flag.
TEST_F(TypeSystemClikeChildrenTest, PointerToScalarHasSingleDerefChild) {
  CompilerType ptr = builder.CreatePointerType(GetInt());
  auto num_children =
      ts->GetNumChildren(ptr.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 1u);

  std::string name;
  uint32_t byte_size = 0;
  int32_t byte_offset = 0;
  uint32_t bf_size = 0, bf_offset = 0;
  bool is_base = false, is_deref = false;
  uint64_t lang_flags = 0;
  auto child = ts->GetChildCompilerTypeAtIndex(
      ptr.GetOpaqueQualType(), nullptr, 0, /*transparent_pointers=*/true, true,
      false, name, byte_size, byte_offset, bf_size, bf_offset, is_base,
      is_deref, nullptr, lang_flags);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_TRUE(is_deref);
  EXPECT_EQ(child->GetOpaqueQualType(), GetInt().GetOpaqueQualType());
}

// `void *` is the one pointer with no deref child: its pointee is the `void`
// builtin (a TypeRef always names a type), so this has to be recognized as
// void rather than as an absent pointee.
TEST_F(TypeSystemClikeChildrenTest, VoidPointerHasNoChildren) {
  CompilerType ptr = builder.CreatePointerType(builder.GetVoidType());
  auto num_children =
      ts->GetNumChildren(ptr.GetOpaqueQualType(), false, nullptr);
  ASSERT_THAT_EXPECTED(num_children, llvm::Succeeded());
  EXPECT_EQ(*num_children, 0u);

  // Its pointee is still reported (as `void`), matching TypeSystemClang.
  EXPECT_TRUE(ts->GetPointeeType(ptr.GetOpaqueQualType()).IsVoidType());

  std::string name;
  uint32_t byte_size = 0;
  int32_t byte_offset = 0;
  uint32_t bf_size = 0, bf_offset = 0;
  bool is_base = false, is_deref = false;
  uint64_t lang_flags = 0;
  auto child = ts->GetChildCompilerTypeAtIndex(
      ptr.GetOpaqueQualType(), nullptr, 0, /*transparent_pointers=*/true, true,
      false, name, byte_size, byte_offset, bf_size, bf_offset, is_base,
      is_deref, nullptr, lang_flags);
  ASSERT_THAT_EXPECTED(child, llvm::Succeeded());
  EXPECT_FALSE(child->IsValid());
}
