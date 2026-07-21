//===-- ClangPersistentVariables.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGPERSISTENTVARIABLES_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGPERSISTENTVARIABLES_H

#include "llvm/ADT/DenseMap.h"

#include "ClangExpressionVariable.h"
#include "ClangModulesDeclVendor.h"

#include "lldb/Expression/ExpressionVariable.h"
#include <optional>
#include <string>
#include <vector>

namespace lldb_private {

class ClangASTImporter;
class ClangModulesDeclVendor;
class Target;
class TypeSystemClang;

/// \class ClangPersistentVariables ClangPersistentVariables.h
/// "lldb/Expression/ClangPersistentVariables.h" Manages persistent values
/// that need to be preserved between expression invocations.
///
/// A list of variables that can be accessed and updated by any expression.  See
/// ClangPersistentVariable for more discussion.  Also provides an increasing,
/// 0-based counter for naming result variables.
class ClangPersistentVariables
    : public llvm::RTTIExtends<ClangPersistentVariables,
                               PersistentExpressionState> {
public:
  // LLVM RTTI support
  static char ID;

  ClangPersistentVariables(std::shared_ptr<Target> target_sp);

  ~ClangPersistentVariables() override = default;

  std::shared_ptr<ClangASTImporter> GetClangASTImporter();
  std::shared_ptr<ClangModulesDeclVendor> GetClangModulesDeclVendor();

  lldb::ExpressionVariableSP
  CreatePersistentVariable(const lldb::ValueObjectSP &valobj_sp) override;

  lldb::ExpressionVariableSP CreatePersistentVariable(
      ExecutionContextScope *exe_scope, ConstString name,
      const CompilerType &compiler_type, lldb::ByteOrder byte_order,
      uint32_t addr_byte_size) override;

  void RemovePersistentVariable(lldb::ExpressionVariableSP variable) override;

  ConstString GetNextPersistentVariableName(bool is_error = false) override;

  /// Returns the next file name that should be used for user expressions.
  std::string GetNextExprFileName() {
    std::string name;
    name.append("<user expression ");
    name.append(std::to_string(m_next_user_file_id++));
    name.append(">");
    return name;
  }

  std::optional<CompilerType>
  GetCompilerTypeFromPersistentDecl(ConstString type_name) override;

  void RegisterPersistentDecl(ConstString name, clang::NamedDecl *decl,
                              std::shared_ptr<TypeSystemClang> ctx);

  clang::NamedDecl *GetPersistentDecl(ConstString name);

  /// Register a persistent type (e.g. `$foo` from `expression struct $foo
  /// {...};`, `$bar` from `expression typedef int $bar`, or an ordinary
  /// non-`$` name from a top-level `expression --top-level -- struct Foo
  /// {...};`) that is backed by a CompilerType rather than a clang::NamedDecl
  /// -- used by the TypeSystemCpp expression path, which has no shared
  /// clang::ASTContext to keep a decl alive across expressions.
  /// GetCompilerTypeFromPersistentDecl checks this map first.
  void RegisterPersistentType(ConstString name, CompilerType type);

  /// Remember the raw source text of a `expression --top-level -- ...` that
  /// declared one or more functions/variables, keyed by the names it defines
  /// (\p names). Used only by the TypeSystemCpp expression path: a top-level
  /// function/variable cannot be round-tripped into a context-independent
  /// CompilerType the way a type can (its *body* would have to be re-emitted
  /// into every later expression's IR), so instead we stash the original
  /// source and textually re-inject it as a translation-unit-level prefix into
  /// any later expression that references one of \p names (see
  /// GetInjectedTopLevelSource). Nothing is stored for a top-level expression
  /// that only declares types -- those go through RegisterPersistentType.
  void RegisterTopLevelSource(std::vector<std::string> names,
                              std::string source);

  /// Build the translation-unit-level prefix to inject before parsing
  /// \p expr_text: scan the expression for identifier tokens and return the
  /// concatenation (in declaration order) of every stored top-level source
  /// (see RegisterTopLevelSource) that defines a referenced name, pulling in
  /// transitively-referenced top-level sources as well. Returns an empty
  /// string when nothing matches.
  std::string GetInjectedTopLevelSource(llvm::StringRef expr_text) const;

  void AddHandLoadedClangModule(ClangModulesDeclVendor::ModuleID module) {
    m_hand_loaded_clang_modules.push_back(module);
  }

  const ClangModulesDeclVendor::ModuleVector &GetHandLoadedClangModules() {
    return m_hand_loaded_clang_modules;
  }

protected:
  llvm::StringRef
  GetPersistentVariablePrefix(bool is_error = false) const override {
    return "$";
  }

private:
  /// The counter used by GetNextExprFileName.
  uint32_t m_next_user_file_id = 0;
  // The counter used by GetNextPersistentVariableName
  uint32_t m_next_persistent_variable_id = 0;

  struct PersistentDecl {
    /// The persistent decl.
    clang::NamedDecl *m_decl = nullptr;
    /// The TypeSystemClang for the ASTContext of m_decl.
    lldb::TypeSystemWP m_context;
  };

  typedef llvm::DenseMap<const char *, PersistentDecl> PersistentDeclMap;
  PersistentDeclMap
      m_persistent_decls; ///< Persistent entities declared by the user.

  /// Persistent types declared by the user and backed by a CompilerType (the
  /// TypeSystemCpp path), keyed by name (e.g. "$foo"). See
  /// RegisterPersistentType.
  llvm::DenseMap<const char *, CompilerType> m_persistent_types;

  /// A top-level expression's raw source plus the function/variable names it
  /// declares. See RegisterTopLevelSource / GetInjectedTopLevelSource.
  struct TopLevelSource {
    std::vector<std::string> names;
    std::string source;
  };
  /// Stored top-level sources, in declaration order (the order the user
  /// declared them, which is the order they must be re-emitted for C).
  std::vector<TopLevelSource> m_top_level_sources;

  ClangModulesDeclVendor::ModuleVector
      m_hand_loaded_clang_modules; ///< These are Clang modules we hand-loaded;
                                   ///these are the highest-
                                   ///< priority source for macros.
  std::shared_ptr<ClangASTImporter> m_ast_importer_sp;
  std::shared_ptr<ClangModulesDeclVendor> m_modules_decl_vendor_sp;
  std::shared_ptr<Target> m_target_sp;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGPERSISTENTVARIABLES_H
