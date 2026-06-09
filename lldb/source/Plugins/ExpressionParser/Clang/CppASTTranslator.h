//===-- CppASTTranslator.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_EXPRESSIONPARSER_CLANG_CPPASSTTRANSLATOR_H
#define LLDB_PLUGINS_EXPRESSIONPARSER_CLANG_CPPASSTTRANSLATOR_H

#include "lldb/Symbol/CompilerType.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

#include <set>

namespace clang {
class DeclContext;
class QualType;
}

namespace lldb_private {

class Target;
class TypeSystemClang;
class TypeSystemCpp;
class LLDBTypeNode;
class LLDBNamespaceNode;

/// Translates LLDBTypeIR nodes into clang types in a scratch TypeSystemClang.
///
/// Used by the expression evaluator when the target program was parsed with
/// TypeSystemCpp. One instance is created per GuardedCopyType call; the
/// persistent cache and active-lookup guard are shared across instances via
/// references to ClangASTSource members.
class CppASTTranslator {
public:
  /// \param target         The scratch TypeSystemClang to generate types into.
  /// \param cpp_ts         The source TypeSystemCpp (for lazy completion).
  /// \param cache          Persistent node→QualType cache shared across calls.
  /// \param active_lookups Shared guard set (also used by ClangASTSource's
  ///                       FindExternalVisibleDeclsByName to prevent re-entry).
  /// \param lldb_target    Optional LLDB target for cross-module type lookup.
  CppASTTranslator(TypeSystemClang &target, TypeSystemCpp *cpp_ts,
                   llvm::DenseMap<LLDBTypeNode *, clang::QualType> &cache,
                   std::set<const char *> &active_lookups,
                   llvm::DenseSet<LLDBTypeNode *> &in_progress,
                   Target *lldb_target = nullptr);

  /// Translate a TypeSystemCpp CompilerType to an equivalent CompilerType
  /// backed by the scratch TypeSystemClang. Returns an invalid CompilerType
  /// if translation fails.
  CompilerType Translate(const CompilerType &src);

private:
  TypeSystemClang &m_target;
  TypeSystemCpp *m_cpp_ts;
  Target *m_lldb_target;
  llvm::DenseMap<LLDBTypeNode *, clang::QualType> &m_cache;
  std::set<const char *> &m_active_lookups;
  llvm::DenseSet<LLDBTypeNode *> &m_in_progress;

  /// Returns a cached QualType for \p node, or generates one via GenerateImpl.
  clang::QualType Generate(LLDBTypeNode *node);

  /// Unconditionally generates a clang::QualType for \p node.
  clang::QualType GenerateImpl(LLDBTypeNode *node);

  /// Returns (or creates) the Clang DeclContext corresponding to \p ns_node.
  clang::DeclContext *
  GetOrCreateNamespaceDeclContext(LLDBNamespaceNode *ns_node);
};

} // namespace lldb_private

#endif // LLDB_PLUGINS_EXPRESSIONPARSER_CLANG_CPPASSTTRANSLATOR_H
