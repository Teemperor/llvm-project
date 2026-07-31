//===-- LanguageOptsTest.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/LanguageOpts.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

// The default constructor describes a typical LP64 target without needing a
// triple (used when no target information is available).
TEST(LanguageOptsTest, DefaultIsLP64) {
  LanguageOpts opts;
  const LanguageOpts::BuiltinSizes &sizes = opts.GetBuiltinSizes();
  EXPECT_EQ(sizes.int_size, 4u);
  EXPECT_EQ(sizes.long_size, 8u);
  EXPECT_EQ(sizes.pointer_size, 8u);
}

// A 64-bit target's builtin sizes are read from Clang's target knowledge via
// the triple.
TEST(LanguageOptsTest, X86_64Sizes) {
  LanguageOpts opts{llvm::Triple("x86_64-pc-linux-gnu")};
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
  LanguageOpts opts{llvm::Triple("i386-pc-linux-gnu")};
  const LanguageOpts::BuiltinSizes &sizes = opts.GetBuiltinSizes();
  EXPECT_EQ(sizes.long_size, 4u);
  EXPECT_EQ(sizes.pointer_size, 4u);
}

// An unresolvable triple leaves the LP64 defaults in place rather than
// crashing.
TEST(LanguageOptsTest, UnknownTripleKeepsDefaults) {
  LanguageOpts opts{llvm::Triple("totally-bogus-triple-value")};
  const LanguageOpts::BuiltinSizes &sizes = opts.GetBuiltinSizes();
  EXPECT_EQ(sizes.int_size, 4u);
  EXPECT_EQ(sizes.long_size, 8u);
}

// GetFloatTypeSemantics matches by storage size: 4 bytes is IEEE single, 8
// bytes is IEEE double.
TEST(LanguageOptsTest, FloatTypeSemanticsBySize) {
  LanguageOpts opts{llvm::Triple("x86_64-pc-linux-gnu")};
  EXPECT_EQ(&opts.GetFloatTypeSemantics(4, lldb::eFormatFloat),
           &llvm::APFloat::IEEEsingle());
  EXPECT_EQ(&opts.GetFloatTypeSemantics(8, lldb::eFormatFloat),
           &llvm::APFloat::IEEEdouble());
}

// A size that matches no known float type reports Bogus semantics rather than
// guessing.
TEST(LanguageOptsTest, FloatTypeSemanticsUnknownSizeIsBogus) {
  LanguageOpts opts{llvm::Triple("x86_64-pc-linux-gnu")};
  EXPECT_EQ(&opts.GetFloatTypeSemantics(3, lldb::eFormatFloat),
           &llvm::APFloat::Bogus());
}

// GetBitIntByteSize rounds the requested bit width up to the target's ABI
// alignment for _BitInt.
TEST(LanguageOptsTest, BitIntByteSizeRoundsUpToAlignment) {
  LanguageOpts opts{llvm::Triple("x86_64-pc-linux-gnu")};
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
  LanguageOpts opts{llvm::Triple("x86_64-pc-linux-gnu")};
  EXPECT_FALSE(opts.GetBitIntByteSize(0).has_value());
}
