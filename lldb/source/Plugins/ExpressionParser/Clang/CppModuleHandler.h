//===-- CppModuleHandler.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPMODULEHANDLER_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPMODULEHANDLER_H

#include "llvm/ADT/StringRef.h"

namespace clang {
class DeclContext;
class NamespaceDecl;
} // namespace clang

namespace lldb_private {

/// Reconciles a `clang::NamespaceDecl` the TypeSystemCpp expression path is
/// about to synthesize (for a debug-info namespace such as `std`) with a
/// namespace of the same name that a real, already-`@import`ed C++ module
/// (see CxxModuleHandler / target.import-std-module) may have materialized
/// directly into the same clang::ASTContext.
///
/// Unlike the legacy TypeSystemClang path -- where every debug-info decl is
/// copied into the expression's ASTContext through a clang::ASTImporter,
/// which merges same-named namespaces into one redeclaration chain as a
/// matter of course -- ClangASTGenerator/CppExpressionDeclMap synthesize
/// namespace decls from scratch and previously always passed `PrevDecl =
/// nullptr` to `NamespaceDecl::Create`. Two unrelated `NamespaceDecl`s with
/// the same name and parent are not implicitly the same namespace to Sema:
/// an unqualified reference to the name that can see both (e.g. `std::` used
/// both for a debug-info type and inside module code) is reported as
/// ambiguous. This class is the TypeSystemCpp counterpart of the merging
/// CxxModuleHandler performs on the importer path: find the existing
/// namespace decl (if any) so the caller can chain onto it instead of
/// creating a second, colliding one.
class CppModuleHandler {
public:
  /// Find a `clang::NamespaceDecl` named \p name that is a *direct* child of
  /// \p parent_ctx and was not synthesized by ClangASTGenerator/
  /// CppExpressionDeclMap itself (i.e. one brought in by a real `@import`ed
  /// module). Returns null if none exists. Only a `noload_lookup` is
  /// performed (never triggers external-source deserialization/callbacks) --
  /// see the .cpp file for why a real lookup is unsafe here.
  static clang::NamespaceDecl *
  FindImportedNamespace(const clang::DeclContext *parent_ctx,
                       llvm::StringRef name);
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPMODULEHANDLER_H
