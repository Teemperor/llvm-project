//===-- StdModuleHandler.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTImporter.h"
#include "clang/Sema/Sema.h"

namespace lldb_private {

class StdTemplateSpecializer : public clang::ChainedASTImporter {
  clang::ASTImporter &m_importer;
  clang::Sema *m_sema;

  bool isValid() const {
    return m_sema != nullptr;
  }

public:
  StdTemplateSpecializer(clang::ASTImporter &importer, clang::ASTContext *target);

  ~StdTemplateSpecializer() override {
    m_importer.Chain = nullptr;
  }

  llvm::Optional<clang::Decl *> Import(clang::Decl *d) override;
};

}
