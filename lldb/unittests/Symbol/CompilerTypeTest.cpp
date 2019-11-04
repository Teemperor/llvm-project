//===-- CompilerTypeTest.cpp -------------------------==---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/CompilerType.h"
#include "TestingSupport/Symbol/FakeTypeSystem.h"

#include "gtest/gtest.h"

using namespace lldb_private;

namespace {
struct TestTypeSystem : public lldb_private::FakeTypeSystem {
  void *GetOpaqueValidType() { return &valid_type_storage; }

private:
  int valid_type_storage;
};
} // namespace

TEST(CompilerType, DefaultConstructor) {
  CompilerType type;
  EXPECT_EQ(nullptr, type.GetTypeSystem());
  EXPECT_EQ(nullptr, type.GetOpaqueQualType());
}

TEST(CompilerType, Constructor) {
  TestTypeSystem ts;
  CompilerType type(&ts, ts.GetOpaqueValidType());
  EXPECT_EQ(&ts, type.GetTypeSystem());
  EXPECT_EQ(ts.GetOpaqueValidType(), type.GetOpaqueQualType());
}

TEST(CompilerType, Clear) {
  TestTypeSystem ts;
  CompilerType type(&ts, ts.GetOpaqueValidType());

  type.Clear();
  EXPECT_EQ(nullptr, type.GetTypeSystem());
  EXPECT_EQ(nullptr, type.GetOpaqueQualType());
}
