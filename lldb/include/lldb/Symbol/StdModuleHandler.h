//===-- StdModuleHandler.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTImporter.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/StringSet.h"

namespace lldb_private {

class StdTemplateSpecializer : public clang::ChainedASTImporter {
  clang::ASTImporter &m_importer;
  clang::Sema *m_sema;
  const llvm::StringSet<> m_supported_templates;

  bool isValid() const {
    return m_sema != nullptr;
  }

  llvm::Optional<clang::Decl *> tryInstantiateStdTemplate(clang::Decl *d);

public:
  StdTemplateSpecializer(clang::ASTImporter &importer, clang::ASTContext *target);

  ~StdTemplateSpecializer() override {
    m_importer.Chain = nullptr;
  }

  llvm::Optional<clang::Decl *> Import(clang::Decl *d) override;
  bool ImportDefinition(clang::Decl *d) override {
    return Import(d).hasValue();
  }
};

}
