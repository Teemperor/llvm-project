//===-- BuiltinTypesTest.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/BuiltinTypes.h"
#include "Plugins/TypeSystem/Clike/Identifier.h"
#include "Plugins/TypeSystem/Clike/LanguageOpts.h"

#include "llvm/TargetParser/Triple.h"

#include "gtest/gtest.h"

using namespace lldb_private::clike_typesystem;

namespace {
struct BuiltinTypesTest : public testing::Test {
  LanguageOpts opts{llvm::Triple("x86_64-pc-linux-gnu")};
  IdentifierMap identifiers;
  KnownBuiltinTypes builtins{opts, identifiers};
};
} // namespace

// Get() returns the same canonical instance for a given kind every time.
TEST_F(BuiltinTypesTest, GetIsStable) {
  BuiltinType *a = builtins.Get(BuiltinKind::Int);
  BuiltinType *b = builtins.Get(BuiltinKind::Int);
  EXPECT_EQ(a, b);
}

// Each canonical instance carries the encoding/format/size appropriate for
// its kind.
TEST_F(BuiltinTypesTest, IntAttributes) {
  BuiltinType *i = builtins.Get(BuiltinKind::Int);
  ASSERT_NE(i, nullptr);
  EXPECT_EQ(i->GetEncoding(), lldb::eEncodingSint);
  EXPECT_EQ(i->GetFormat(), lldb::eFormatDecimal);
  EXPECT_EQ(i->GetByteSize(), 4u);
  EXPECT_EQ(i->GetBuiltinKind(), BuiltinKind::Int);
  EXPECT_EQ(i->GetName().GetName(), "int");
}

TEST_F(BuiltinTypesTest, BoolAttributes) {
  BuiltinType *b = builtins.Get(BuiltinKind::Bool);
  ASSERT_NE(b, nullptr);
  // bool and char share the unsigned/signed integer encodings with other
  // types, so the *format* (not the encoding) is what distinguishes them.
  EXPECT_EQ(b->GetEncoding(), lldb::eEncodingUint);
  EXPECT_EQ(b->GetFormat(), lldb::eFormatBoolean);
}

// void has no size.
TEST_F(BuiltinTypesTest, VoidHasNoSize) {
  BuiltinType *v = builtins.Get(BuiltinKind::Void);
  ASSERT_NE(v, nullptr);
  EXPECT_FALSE(v->GetByteSize().has_value());
}

// Match() finds the canonical instance by (encoding, byte size, spelling).
TEST_F(BuiltinTypesTest, MatchCanonicalSpelling) {
  BuiltinType *matched =
      builtins.Match("int", lldb::eEncodingSint, /*byte_size=*/4);
  EXPECT_EQ(matched, builtins.Get(BuiltinKind::Int));
}

// Match() also accepts the alternative (e.g. GCC) spellings for a kind.
TEST_F(BuiltinTypesTest, MatchAlternativeSpelling) {
  BuiltinType *matched = builtins.Match("long unsigned int", lldb::eEncodingUint,
                                        /*byte_size=*/8);
  EXPECT_EQ(matched, builtins.Get(BuiltinKind::UnsignedLong));
}

// A byte size that disagrees with the target's expected size for that kind
// does not match (e.g. claiming `int` is 8 bytes on this target).
TEST_F(BuiltinTypesTest, MatchWrongSizeFails) {
  EXPECT_EQ(builtins.Match("int", lldb::eEncodingSint, /*byte_size=*/8),
           nullptr);
}

// A name that isn't a known builtin spelling doesn't match.
TEST_F(BuiltinTypesTest, MatchUnknownNameFails) {
  EXPECT_EQ(builtins.Match("MyCustomType", lldb::eEncodingSint,
                           /*byte_size=*/4),
           nullptr);
}

// MatchByName looks up purely by spelling, ignoring encoding/size, and also
// recognizes alternative spellings.
TEST_F(BuiltinTypesTest, MatchByNameAlternativeSpelling) {
  EXPECT_EQ(builtins.MatchByName("unsigned"), builtins.Get(BuiltinKind::UnsignedInt));
  EXPECT_EQ(builtins.MatchByName("_Bool"), builtins.Get(BuiltinKind::Bool));
  EXPECT_EQ(builtins.MatchByName("nonexistent"), nullptr);
}

// std::nullptr_t has multiple recognized spellings and is unsigned-pointer
// sized, but is neither scalar nor integer in its type info (matching Clang).
TEST_F(BuiltinTypesTest, NullPtrTypeInfo) {
  BuiltinType *nullptr_t = builtins.Get(BuiltinKind::NullPtr);
  ASSERT_NE(nullptr_t, nullptr);
  EXPECT_EQ(nullptr_t->GetByteSize(), 8u);
  EXPECT_EQ(nullptr_t->GetTypeInfo(), lldb::eTypeHasValue);
  EXPECT_EQ(builtins.MatchByName("decltype(nullptr)"), nullptr_t);
}

// KindForEncodingAndBitSize maps a raw (encoding, width) -- all a register
// context or a runtime plugin knows -- onto a builtin kind.
TEST_F(BuiltinTypesTest, KindForEncodingAndBitSize) {
  using KBT = KnownBuiltinTypes;
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingSint, 32),
            BuiltinKind::Int);
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingUint, 32),
            BuiltinKind::UnsignedInt);
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingIEEE754, 64),
            BuiltinKind::Double);
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingSint, 128),
            BuiltinKind::Int128);
  // Bit sizes round up to whole bytes: 7 bits is a 1-byte type.
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingSint, 7),
            BuiltinKind::SignedChar);
  // Rounding up only helps when the resulting byte width has a builtin: no
  // C type is 3 bytes wide, so a 17- or 24-bit register has no type here.
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingSint, 17),
            std::nullopt);
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingUint, 24),
            std::nullopt);
  // Widths and encodings with no builtin.
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingSint, 256),
            std::nullopt);
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingIEEE754, 8),
            std::nullopt);
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingVector, 32),
            std::nullopt);
}

// Where several builtins share a width, KindForEncodingAndBitSize has to pick
// one. Pin those choices: they are not derivable from the width alone, so a
// future rewrite driven off the kDescs table would otherwise silently change
// them (kDescs lists `char` before `signed char`, and `long` before
// `long long`).
TEST_F(BuiltinTypesTest, KindForEncodingAndBitSizeTieBreaks) {
  using KBT = KnownBuiltinTypes;
  // 1-byte signed is `signed char`, not the implementation-defined-signedness
  // `char`.
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingSint, 8),
            BuiltinKind::SignedChar);
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingUint, 8),
            BuiltinKind::UnsignedChar);
  // 8-byte integers are `long long`, not `long` (both are 8 bytes on LP64).
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingSint, 64),
            BuiltinKind::LongLong);
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingUint, 64),
            BuiltinKind::UnsignedLongLong);
  // 16-byte float is `long double`.
  EXPECT_EQ(KBT::KindForEncodingAndBitSize(lldb::eEncodingIEEE754, 128),
            BuiltinKind::LongDouble);
}
