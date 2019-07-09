//===- unittest/Serialization/GenerationCounterTest.cpp -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests the generation counter in the ExternalASTSource.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/Sema/MultiplexExternalSemaSource.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

namespace {
// Just allows us to easier increment the generation without actually having
// to modify the AST in some way.
class ASTSourceTester : public clang::ExternalSemaSource {
public:
  void testIncrementGeneration(clang::ASTContext &C) { incrementGeneration(C); }
};
} // namespace

namespace clang {

TEST(GenerationCounter, MultipleConsumers) {
  // Three sources which always should see the same generation counter value
  // once they have been attached to the ASTContext.
  ASTSourceTester Source1, Source2, NewSource;

  // Create a real ASTContext.
  std::unique_ptr<ASTUnit> ASTUnit = tooling::buildASTFromCode("int main() {}");
  clang::ASTContext &C = ASTUnit->getASTContext();
  GenerationCounter &Counter = C.getGeneration();

  // Attach the first two sources with a multiplexer.
  MultiplexExternalSemaSource *Multiplexer =
      new MultiplexExternalSemaSource(Source1, Source2);
  C.setExternalSource(Multiplexer);

  auto OldGeneration = Counter.get();

  // Pretend each source modifies the AST and increments the generation counter.
  // After each step the generation counter needs to be identical for each
  // source (but different than the previous counter value).
  Source1.testIncrementGeneration(C);
  ASSERT_NE(Counter.get(), OldGeneration);
  OldGeneration = Counter.get();

  Source2.testIncrementGeneration(C);
  ASSERT_NE(Counter.get(), OldGeneration);
  OldGeneration = Counter.get();

  // Just add the last source which should also directly inherit the correct
  // generation counter value.
  Multiplexer->addSource(NewSource);
  ASSERT_EQ(Counter.get(), OldGeneration);
}

} // namespace clang
