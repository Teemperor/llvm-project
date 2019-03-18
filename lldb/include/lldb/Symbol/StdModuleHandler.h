//===-- StdModuleHandler.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_StdModuleHandler_h_
#define liblldb_StdModuleHandler_h_

#include "clang/AST/ASTImporter.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/StringSet.h"

namespace lldb_private {

/// Handles importing declarations that are also in the 'std' C++ module.
///
/// In general this class expects that the target ASTContext of our import
/// process is setup to use C++ modules and has the 'std' module attached.
class StdModuleHandler : public clang::ImportStrategy {
  clang::ASTImporter &m_importer;

public:
  struct Listener {
    virtual ~Listener() = default;
    /// Called for every decl that has been imported from the 'std' module
    /// instead of being imported by the ASTImporter from the debug info AST.
    virtual void importedDeclFromStdModule(clang::Decl *d) = 0;
  };

private:
  /// The listener we should notify when we deserialize decls from the 'std'
  /// module.
  Listener *m_listener = nullptr;

  clang::Sema *m_sema;
  /// List of template names this class currently supports. These are the
  /// template names inside the 'std' namespace such as 'vector' or 'list'.
  llvm::StringSet<> m_supported_templates;

  bool isValid() const { return m_sema != nullptr; }

  /// Tries to manually instantiate the given foreign declaration in the target
  /// context (designated by m_sema).
  llvm::Optional<clang::Decl *> tryInstantiateStdTemplate(clang::Decl *d);

public:
  StdModuleHandler(clang::ASTImporter &importer, Listener *listener,
                   clang::ASTContext *target);
  ~StdModuleHandler() override;

  /// Attempts to import the given decl into the target ASTContext by
  /// deserializing it from the 'std' module. This function returns a decl if a
  /// decl has been deserialized from the 'std' module. Otherwise this function
  /// returns nothing. Implements the ImportStrategy interface, so it will be
  /// called from the ASTImporter.
  llvm::Optional<clang::Decl *> Import(clang::ASTImporter &importer,
                                       clang::Decl *d) override;

  void setListener(Listener *l) { m_listener = l; }
};

} // namespace lldb_private

#endif // liblldb_StdModuleHandler_h_
