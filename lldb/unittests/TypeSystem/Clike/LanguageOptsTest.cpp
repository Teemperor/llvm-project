//===-- LanguageOptsTest.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/LanguageOpts.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/Support/Error.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

namespace {
/// Options for a triple the test knows Clang can describe.
LanguageOpts OptsFor(const char *triple) {
  return llvm::cantFail(LanguageOpts::Create(llvm::Triple(triple)));
}
} // namespace

// A 64-bit target's builtin sizes are read from Clang's target knowledge via
// the triple.
TEST(LanguageOptsTest, X86_64Sizes) {
  LanguageOpts opts = OptsFor("x86_64-pc-linux-gnu");
  const LanguageOpts::BuiltinSizes &sizes = opts.GetBuiltinSizes();
  EXPECT_EQ(sizes.bool_size, 1u);
  EXPECT_EQ(sizes.short_size, 2u);
  EXPECT_EQ(sizes.int_size, 4u);
  EXPECT_EQ(sizes.long_size, 8u);
  EXPECT_EQ(sizes.long_long_size, 8u);
  EXPECT_EQ(sizes.float_size, 4u);
  EXPECT_EQ(sizes.double_size, 8u);
  EXPECT_EQ(sizes.pointer_size, 8u);
}

// On a 32-bit target `long` and pointers shrink to 4 bytes (LP32/ILP32),
// unlike the LP64 x86_64 target above.
TEST(LanguageOptsTest, I386Sizes) {
  LanguageOpts opts = OptsFor("i386-pc-linux-gnu");
  const LanguageOpts::BuiltinSizes &sizes = opts.GetBuiltinSizes();
  EXPECT_EQ(sizes.long_size, 4u);
  EXPECT_EQ(sizes.pointer_size, 4u);
}

// A triple Clang can't describe is an error rather than a silent fallback to
// some plausible-looking default layout.
TEST(LanguageOptsTest, CreateFailsForUnknownTriple) {
  llvm::Expected<LanguageOpts> opts =
      LanguageOpts::Create(llvm::Triple("totally-bogus-triple-value"));
  EXPECT_FALSE(static_cast<bool>(opts));
  llvm::consumeError(opts.takeError());
}

// Every target-derived size is filled in by Create -- none is left at the 0
// that would signal a field the derivation forgot about.
TEST(LanguageOptsTest, CreatePopulatesEverySize) {
  const LanguageOpts::BuiltinSizes &sizes =
      OptsFor("x86_64-pc-linux-gnu").GetBuiltinSizes();
  EXPECT_NE(sizes.bool_size, 0u);
  EXPECT_NE(sizes.short_size, 0u);
  EXPECT_NE(sizes.int_size, 0u);
  EXPECT_NE(sizes.long_size, 0u);
  EXPECT_NE(sizes.long_long_size, 0u);
  EXPECT_NE(sizes.wchar_size, 0u);
  EXPECT_NE(sizes.char16_size, 0u);
  EXPECT_NE(sizes.char32_size, 0u);
  EXPECT_NE(sizes.float_size, 0u);
  EXPECT_NE(sizes.double_size, 0u);
  EXPECT_NE(sizes.long_double_size, 0u);
  EXPECT_NE(sizes.pointer_size, 0u);
}

// GetFloatTypeSemantics matches by storage size: 4 bytes is IEEE single, 8
// bytes is IEEE double.
TEST(LanguageOptsTest, FloatTypeSemanticsBySize) {
  LanguageOpts opts = OptsFor("x86_64-pc-linux-gnu");
  EXPECT_EQ(&opts.GetFloatTypeSemantics(4, lldb::eFormatFloat),
            &llvm::APFloat::IEEEsingle());
  EXPECT_EQ(&opts.GetFloatTypeSemantics(8, lldb::eFormatFloat),
            &llvm::APFloat::IEEEdouble());
}

// A size that matches no known float type reports Bogus semantics rather than
// guessing.
TEST(LanguageOptsTest, FloatTypeSemanticsUnknownSizeIsBogus) {
  LanguageOpts opts = OptsFor("x86_64-pc-linux-gnu");
  EXPECT_EQ(&opts.GetFloatTypeSemantics(3, lldb::eFormatFloat),
            &llvm::APFloat::Bogus());
}

// GetBitIntByteSize rounds the requested bit width up to the target's ABI
// alignment for _BitInt.
TEST(LanguageOptsTest, BitIntByteSizeRoundsUpToAlignment) {
  LanguageOpts opts = OptsFor("x86_64-pc-linux-gnu");
  std::optional<uint64_t> size = opts.GetBitIntByteSize(1);
  ASSERT_TRUE(size.has_value());
  // A 1-bit _BitInt still occupies at least one byte.
  EXPECT_GE(*size, 1u);

  std::optional<uint64_t> size9 = opts.GetBitIntByteSize(9);
  ASSERT_TRUE(size9.has_value());
  // 9 bits need more than 1 byte of storage.
  EXPECT_GT(*size9, 1u);
}

// A zero bit width is invalid and reports no size.
TEST(LanguageOptsTest, BitIntByteSizeZeroIsInvalid) {
  LanguageOpts opts = OptsFor("x86_64-pc-linux-gnu");
  EXPECT_FALSE(opts.GetBitIntByteSize(0).has_value());
}
