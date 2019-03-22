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
class StdModuleHandler {
  clang::ASTImporter *m_importer = nullptr;

  clang::Sema *m_sema = nullptr;
  /// List of template names this class currently supports. These are the
  /// template names inside the 'std' namespace such as 'vector' or 'list'.
  llvm::StringSet<> m_supported_templates;

  /// Tries to manually instantiate the given foreign declaration in the target
  /// context (designated by m_sema).
  llvm::Optional<clang::Decl *> tryInstantiateStdTemplate(clang::Decl *d);

public:
  StdModuleHandler() = default;
  StdModuleHandler(clang::ASTImporter &importer, clang::ASTContext *target);

  /// Attempts to import the given decl into the target ASTContext by
  /// deserializing it from the 'std' module. This function returns a decl if a
  /// decl has been deserialized from the 'std' module. Otherwise this function
  /// returns nothing.
  llvm::Optional<clang::Decl *> Import(clang::Decl *d);

  bool isValid() const { return m_sema != nullptr; }
};

} // namespace lldb_private

#endif // liblldb_StdModuleHandler_h_
