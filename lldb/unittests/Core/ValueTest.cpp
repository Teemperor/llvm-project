//===-- ValueTest.cpp --------------------------------==---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MockTypeSystem.h"
#include "lldb/Core/Value.h"

#include "gtest/gtest.h"

#include <array>

using namespace lldb_private;

namespace {
struct ValueTestTypeSystem : public lldb_testing::MockTypeSystem {
  ValueTestTypeSystem() : lldb_testing::MockTypeSystem() {}

  std::array<int, 16> int_type_pointers;
  CompilerType GetIntTypeWithSize(size_t byte_size) {
    return CompilerType(this, reinterpret_cast<void *>(&int_type_pointers[byte_size]));
  }
  CompilerType GetInvalidType() {
    return CompilerType(this, nullptr);
  }
};
} // namespace


TEST(ValueTest, Clear) {
  Value value(Scalar(1));
  EXPECT_EQ(value.
  value.Clear();
}

TEST(ValueTest, SDfasdf) {
  ValueTestTypeSystem s;
  Value value(Scalar(12345));
  value.SetCompilerType(s.GetIntTypeWithSize(4));

}
