//===-- SugarTypeTest.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/SugarType.h"
#include "Plugins/TypeSystem/Clike/RecordType.h"

#include "llvm/Support/Casting.h"

#include "gtest/gtest.h"

#include <vector>

using namespace lldb_private::clike;

namespace {
struct SugarTypeTest : public testing::Test {
  /// An unnamed record of \p byte_size bytes.
  static RecordType MakeRecord(std::optional<uint64_t> byte_size) {
    return RecordType(Identifier(), DeclScope(), byte_size);
  }
};
} // namespace

// Sugar forwards the layout/value queries to what it wraps, so a wrapped
// aggregate still looks like one.
TEST_F(SugarTypeTest, SugarForwardsToUnderlyingType) {
  RecordType record = MakeRecord(24);
  SugarType sugar{TypeRef(record)};

  EXPECT_EQ(sugar.GetUnderlyingType(), &record);
  EXPECT_TRUE(sugar.IsAggregate());
  EXPECT_EQ(sugar.GetByteSize(), std::optional<uint64_t>(24));
  EXPECT_EQ(sugar.GetTypeInfo(), record.GetTypeInfo());
  EXPECT_EQ(sugar.GetNumFields(), record.GetNumFields());
  EXPECT_EQ(sugar.GetNumBaseClasses(), record.GetNumBaseClasses());
}

// Desugaring peels every layer of sugar in one step, reaching the canonical
// type; a canonical type desugars to itself.
TEST_F(SugarTypeTest, DesugarReachesCanonicalType) {
  RecordType record;
  SugarType one{TypeRef(record)};
  SugarType two{TypeRef(one)};
  SugarType three{TypeRef(two)};

  EXPECT_EQ(record.Desugar(), &record);
  EXPECT_EQ(one.Desugar(), &record);
  EXPECT_EQ(two.Desugar(), &record);
  EXPECT_EQ(three.Desugar(), &record);
  // Only one layer is peeled by GetUnderlyingType.
  EXPECT_EQ(three.GetUnderlyingType(), &two);
}

// The free Desugar() peels sugar like Type::Desugar() and tolerates the null a
// failed lookup hands it.
TEST_F(SugarTypeTest, FreeDesugar) {
  EXPECT_EQ(Desugar(static_cast<Type *>(nullptr)), nullptr);
  EXPECT_EQ(Desugar(static_cast<const Type *>(nullptr)), nullptr);

  RecordType record;
  SugarType sugar{TypeRef(record)};
  EXPECT_EQ(Desugar(&sugar), &record);

  const SugarType &const_sugar = sugar;
  EXPECT_EQ(Desugar(&const_sugar), &record);
}

// LLVM-style RTTI distinguishes the core kinds, including through a reference
// held as the base Type.
TEST_F(SugarTypeTest, RTTI) {
  RecordType record;
  SugarType sugar{TypeRef(record)};

  Type *as_type = &record;
  EXPECT_TRUE(llvm::isa<RecordType>(as_type));
  EXPECT_FALSE(llvm::isa<SugarType>(as_type));
  EXPECT_EQ(llvm::dyn_cast<RecordType>(as_type), &record);
  EXPECT_EQ(llvm::dyn_cast<SugarType>(as_type), nullptr);

  as_type = &sugar;
  EXPECT_TRUE(llvm::isa<SugarType>(as_type));
  EXPECT_FALSE(llvm::isa<RecordType>(as_type));

  // ...and tolerates a null through the _or_null forms.
  Type *null_type = nullptr;
  EXPECT_EQ(llvm::dyn_cast_or_null<RecordType>(null_type), nullptr);
}

// HasFields sees through sugar and completes each record it inspects. A
// record with no definition anywhere reports "has fields" so that callers can
// surface it as incomplete rather than silently hiding it.
TEST_F(SugarTypeTest, HasFields) {
  RecordType incomplete;
  SugarType sugar{TypeRef(incomplete)};

  std::vector<Type *> completed;
  auto complete = [&completed](Type *t) { completed.push_back(t); };

  EXPECT_TRUE(sugar.HasFields(complete));
  // Desugared before being completed and inspected.
  ASSERT_EQ(completed.size(), 1u);
  EXPECT_EQ(completed[0], &incomplete);
}
