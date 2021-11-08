//===-- ImporterBackedASTSource.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLANG_IMPORTERBACKEDASTSOURCE
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLANG_IMPORTERBACKEDASTSOURCE

#include "clang/AST/ASTContext.h"
#include "clang/Sema/ExternalSemaSource.h"

namespace lldb_private {

class ImporterBackedASTSource : public clang::ExternalSemaSource {
  /// LLVM RTTI support.
  static char ID;

public:
  /// LLVM RTTI support.
  bool isA(const void *ClassID) const override { return ClassID == &ID || ExternalSemaSource::isA(ClassID); }
  static bool classof(const clang::ExternalASTSource *s) { return s->isA(&ID); }

  void BumpGenerationCounter(clang::ASTContext &c) { incrementGeneration(c); }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLANG_IMPORTERBACKEDASTSOURCE
