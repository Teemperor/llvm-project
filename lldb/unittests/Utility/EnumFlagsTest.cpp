//===-- EnumFlagsTest.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "gtest/gtest.h"

#include "lldb/Utility/EnumFlags.h"

using namespace lldb_private;

enum DummyFlags {
  eFlag0 = 1 << 0,
  eFlag1 = 1 << 1,
  eFlag2 = 1 << 4,
};

TEST(EnumFlags, DefaultConstructor) {
  EnumFlags<DummyFlags> flags;
  EXPECT_FALSE(flags.Test(eFlag0));
  EXPECT_FALSE(flags.Test(eFlag1));
  EXPECT_FALSE(flags.Test(eFlag2));
}

TEST(EnumFlags, ListConstructor) {
  EnumFlags<DummyFlags> flags({eFlag0, eFlag1});
  EXPECT_TRUE(flags.Test(eFlag0));
  EXPECT_TRUE(flags.Test(eFlag1));
  EXPECT_FALSE(flags.Test(eFlag2));
}

TEST(EnumFlags, Set) {
  EnumFlags<DummyFlags> flags;

  flags.Set(eFlag0);
  EXPECT_TRUE(flags.Test(eFlag0));
  EXPECT_FALSE(flags.Test(eFlag1));
  EXPECT_FALSE(flags.Test(eFlag2));

  flags.Set(eFlag1);
  EXPECT_TRUE(flags.Test(eFlag0));
  EXPECT_TRUE(flags.Test(eFlag1));
  EXPECT_FALSE(flags.Test(eFlag2));
}

TEST(EnumFlags, SetSeveral) {
  EnumFlags<DummyFlags> flags;

  flags.Set({eFlag0, eFlag1});
  EXPECT_TRUE(flags.Test(eFlag0));
  EXPECT_TRUE(flags.Test(eFlag1));
  EXPECT_FALSE(flags.Test(eFlag2));

  flags.Set({eFlag1, eFlag2});
  EXPECT_TRUE(flags.Test(eFlag0));
  EXPECT_TRUE(flags.Test(eFlag1));
  EXPECT_TRUE(flags.Test(eFlag2));
}

TEST(EnumFlags, Clear) {
  EnumFlags<DummyFlags> flags({eFlag0, eFlag1});

  flags.Clear(eFlag0);
  EXPECT_FALSE(flags.Test(eFlag0));
  EXPECT_TRUE(flags.Test(eFlag1));
  EXPECT_FALSE(flags.Test(eFlag2));

  flags.Clear(eFlag1);
  EXPECT_FALSE(flags.Test(eFlag0));
  EXPECT_FALSE(flags.Test(eFlag1));
  EXPECT_FALSE(flags.Test(eFlag2));
}

TEST(EnumFlags, ClearSeveral) {
  EnumFlags<DummyFlags> flags({eFlag0, eFlag1, eFlag2});

  flags.Clear({eFlag0, eFlag1});
  EXPECT_FALSE(flags.Test(eFlag0));
  EXPECT_FALSE(flags.Test(eFlag1));
  EXPECT_TRUE(flags.Test(eFlag2));

  flags.Clear({eFlag1, eFlag2});
  EXPECT_FALSE(flags.Test(eFlag0));
  EXPECT_FALSE(flags.Test(eFlag1));
  EXPECT_FALSE(flags.Test(eFlag2));
}

TEST(EnumFlags, IsClear) {
  EnumFlags<DummyFlags> flags;

  flags.Set(eFlag0);
  EXPECT_FALSE(flags.IsClear(eFlag0));
  EXPECT_TRUE(flags.IsClear(eFlag1));
  EXPECT_TRUE(flags.IsClear(eFlag2));

  flags.Set(eFlag1);
  EXPECT_FALSE(flags.IsClear(eFlag0));
  EXPECT_FALSE(flags.IsClear(eFlag1));
  EXPECT_TRUE(flags.IsClear(eFlag2));
}

TEST(EnumFlags, AllSet) {
  EnumFlags<DummyFlags> flags({eFlag0, eFlag2});
  EXPECT_TRUE(flags.AllSet({eFlag0, eFlag2}));
  EXPECT_TRUE(flags.AllSet({eFlag0}));
  EXPECT_FALSE(flags.AllSet({eFlag1}));
  EXPECT_FALSE(flags.AllSet({eFlag0, eFlag1}));
  EXPECT_FALSE(flags.AllSet({eFlag2, eFlag1, eFlag0}));
}

TEST(EnumFlags, AnySet) {
  EnumFlags<DummyFlags> flags({eFlag0, eFlag2});
  EXPECT_TRUE(flags.AnySet({eFlag0, eFlag2}));
  EXPECT_TRUE(flags.AnySet({eFlag0}));
  EXPECT_FALSE(flags.AnySet({eFlag1}));
  EXPECT_TRUE(flags.AnySet({eFlag0, eFlag1}));
  EXPECT_TRUE(flags.AnySet({eFlag2, eFlag1, eFlag0}));
}

TEST(EnumFlags, AllClear) {
  EnumFlags<DummyFlags> flags({eFlag0, eFlag2});
  EXPECT_TRUE(flags.AllSet({eFlag0, eFlag2}));
  EXPECT_TRUE(flags.AllSet({eFlag0}));
  EXPECT_FALSE(flags.AllSet({eFlag1}));
  EXPECT_FALSE(flags.AllSet({eFlag0, eFlag1}));
  EXPECT_FALSE(flags.AllSet({eFlag2, eFlag1, eFlag0}));
}

TEST(EnumFlags, AnyClear) {
  EnumFlags<DummyFlags> flags({eFlag0, eFlag2});
  EXPECT_TRUE(flags.AnySet({eFlag0, eFlag2}));
  EXPECT_TRUE(flags.AnySet({eFlag0}));
  EXPECT_FALSE(flags.AnySet({eFlag1}));
  EXPECT_TRUE(flags.AnySet({eFlag0, eFlag1}));
  EXPECT_TRUE(flags.AnySet({eFlag2, eFlag1, eFlag0}));
}

TEST(EnumFlags, GetRawEncoding) {
  EnumFlags<DummyFlags> flags;
  EXPECT_EQ(flags.GetRawEncoding(), 0U);
  flags.Set({eFlag0, eFlag2});
  EXPECT_EQ(flags.GetRawEncoding(), static_cast<unsigned>(eFlag0 | eFlag2));
  flags.Set(eFlag1);
  EXPECT_EQ(flags.GetRawEncoding(),
            static_cast<unsigned>(eFlag0 | eFlag1 | eFlag2));
}

TEST(EnumFlags, SetFromRawEncoding) {
  EnumFlags<DummyFlags> flags;
  flags.SetFromRawEncoding(eFlag0 | eFlag1);
  EXPECT_EQ(flags.GetRawEncoding(), static_cast<unsigned>(eFlag0 | eFlag1));
  flags.SetFromRawEncoding(eFlag0);
  EXPECT_TRUE(flags.Test(eFlag0));
}

enum class Scoped { Flag0 = (1 << 0), Flag1 = (1 << 1), Flag2 = (1 << 3) };

TEST(EnumFlags, ScopedEnum) {
  // Call all functions and test some basic functionality with a scoped enum.
  EnumFlags<Scoped> flags;
  flags.Set(Scoped::Flag0);
  EXPECT_TRUE(flags.Test(Scoped::Flag0));
  EXPECT_FALSE(flags.Test(Scoped::Flag1));
  EXPECT_FALSE(flags.Test(Scoped::Flag2));
  EXPECT_TRUE(flags.IsClear(Scoped::Flag2));

  flags.Set({Scoped::Flag1, Scoped::Flag0});
  EXPECT_TRUE(flags.Test(Scoped::Flag0));
  EXPECT_TRUE(flags.Test(Scoped::Flag1));
  EXPECT_FALSE(flags.Test(Scoped::Flag2));

  flags.Clear(Scoped::Flag0);
  EXPECT_FALSE(flags.Test(Scoped::Flag0));
  EXPECT_TRUE(flags.Test(Scoped::Flag1));
  EXPECT_FALSE(flags.Test(Scoped::Flag2));

  flags.Clear({Scoped::Flag0, Scoped::Flag2});
  EXPECT_FALSE(flags.Test(Scoped::Flag0));
  EXPECT_TRUE(flags.Test(Scoped::Flag1));
  EXPECT_FALSE(flags.Test(Scoped::Flag2));

  EXPECT_TRUE(flags.AllSet({Scoped::Flag1}));
  EXPECT_FALSE(flags.AllSet({Scoped::Flag1, Scoped::Flag2}));

  EXPECT_TRUE(flags.AnySet({Scoped::Flag1}));
  EXPECT_FALSE(flags.AnySet({Scoped::Flag0, Scoped::Flag2}));

  EXPECT_FALSE(flags.AllClear({Scoped::Flag1}));
  EXPECT_TRUE(flags.AllClear({Scoped::Flag0, Scoped::Flag2}));

  EXPECT_FALSE(flags.AnyClear({Scoped::Flag1}));
  EXPECT_TRUE(flags.AnyClear({Scoped::Flag0, Scoped::Flag2}));

  EnumFlags<Scoped> flags2({Scoped::Flag0, Scoped::Flag1});
  EXPECT_TRUE(flags2.Test(Scoped::Flag0));
}
