//===- ExternalASTSource.h - Abstract External AST Interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_AST_GENERATIONCOUNTER_H
#define LLVM_CLANG_AST_GENERATIONCOUNTER_H

#include <cassert>
#include <cstdint>

namespace clang {

class GenerationCounter {
  uint32_t CurrentGeneration = 0;

public:
  uint32_t get() const { return CurrentGeneration; }
  uint32_t increment() {
    uint32_t OldGeneration = CurrentGeneration;
    CurrentGeneration++;
    assert(CurrentGeneration > OldGeneration &&
           "Overflowed generation counter");
    return OldGeneration;
  }
};

} // namespace clang

#endif // LLVM_CLANG_AST_GENERATIONCOUNTER_H
