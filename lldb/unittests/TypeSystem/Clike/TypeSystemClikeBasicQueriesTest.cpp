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
#include "gtest/gtest.h"

using namespace lldb_private;
using namespace lldb_private::clike;

namespace {
struct TypeSystemClikeBasicQueriesTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::cantFail(clike::LanguageOpts::Create(
                  llvm::Triple("x86_64-pc-linux-gnu"))));
  Builder builder{*ts};

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }
  CompilerType GetChar() {
    return builder.GetBuiltinType("char", 1, lldb::eEncodingSint,
                                  lldb::eFormatChar);
  }
  CompilerType GetVoid() { return builder.GetVoidType(); }
};
} // namespace

// IsCharType recognizes the narrow char builtins by format+size, but not int
// (which shares the signed-integer encoding).
TEST_F(TypeSystemClikeBasicQueriesTest, IsCharType) {
  EXPECT_TRUE(GetChar().IsCharType());
  EXPECT_FALSE(GetInt().IsCharType());
}

// A floating point builtin reports IsFloatingPointType; an integer builtin
// does not.
TEST_F(TypeSystemClikeBasicQueriesTest, IsFloatingPointType) {
  CompilerType f = builder.GetBuiltinType("float", 4, lldb::eEncodingIEEE754,
                                          lldb::eFormatFloat);
  EXPECT_TRUE(f.IsFloatingPointType());
  EXPECT_FALSE(GetInt().IsFloatingPointType());
}

// Signed and unsigned integer builtins report IsIntegerType with the correct
// signedness; nullptr_t (despite an unsigned encoding) does not.
TEST_F(TypeSystemClikeBasicQueriesTest, IsIntegerType) {
  bool is_signed = false;
  EXPECT_TRUE(ts->IsIntegerType(GetInt().GetOpaqueQualType(), is_signed));
  EXPECT_TRUE(is_signed);

  CompilerType unsigned_int = builder.GetBuiltinType(
      "unsigned int", 4, lldb::eEncodingUint, lldb::eFormatUnsigned);
  EXPECT_TRUE(ts->IsIntegerType(unsigned_int.GetOpaqueQualType(), is_signed));
  EXPECT_FALSE(is_signed);

  CompilerType nullptr_t = builder.GetBuiltinType(
      "std::nullptr_t", 8, lldb::eEncodingUint, lldb::eFormatHex);
  EXPECT_FALSE(ts->IsIntegerType(nullptr_t.GetOpaqueQualType(), is_signed));
}

// IsVoidType identifies the void builtin by spelling, including through
// typedef sugar, but not other builtins.
TEST_F(TypeSystemClikeBasicQueriesTest, IsVoidType) {
  EXPECT_TRUE(GetVoid().IsVoidType());
  CompilerType alias = builder.CreateTypedefType("MyVoid", GetVoid());
  EXPECT_TRUE(alias.IsVoidType());
  EXPECT_FALSE(GetInt().IsVoidType());
}
