//===---------- llvm/unittest/Support/DJBTest.cpp -------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "llvm/Support/Lockable.h"
#include "gtest/gtest.h"

using namespace llvm;

TEST(LockableTest, Integer) {
  Lockable<int> Data(1);
  {
    UniqueAccess<int> Access(Data);
    (*Access)++;
    EXPECT_EQ(2, *Access);
  }
}

namespace {
  struct DataStruct {
    int member = 0;
  };
}

TEST(LockableTest, DataStruct) {
  Lockable<DataStruct> Data;
  UniqueAccess<DataStruct> Access(Data);
  Access->member++;
  EXPECT_EQ(1, Access->member);
}

namespace {
  struct ClassContainingLockable {
    struct ProtectedData {
      int member = 0;
    };
    Lockable<ProtectedData> Data;
    void increment() {
      UniqueAccess<ProtectedData> Access(Data);
      Access->member++;
    }
    int getValue() const {
      SharedAccess<ProtectedData> Access(Data);
      return Access->member;
    }
  };
}

TEST(LockableTest, ClassWithContainingLockable) {
  ClassContainingLockable C;
  C.increment();
  EXPECT_EQ(1, C.getValue());
}

TEST(LockableTest, CopyConstructor) {
  ClassContainingLockable Original;
  Original.increment();
  EXPECT_EQ(1, Original.getValue());
  ClassContainingLockable Copy(Original);
  EXPECT_EQ(1, Copy.getValue());
}

TEST(LockableTest, MoveConstructor) {
  ClassContainingLockable Original;
  Original.increment();
  EXPECT_EQ(1, Original.getValue());
  ClassContainingLockable Copy(std::move(Original));
  EXPECT_EQ(1, Copy.getValue());
}

TEST(LockableTest, Assignment) {
  ClassContainingLockable Original;
  Original.increment();
  EXPECT_EQ(1, Original.getValue());
  ClassContainingLockable Copy = Original;
  EXPECT_EQ(1, Copy.getValue());
}
