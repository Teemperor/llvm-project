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
