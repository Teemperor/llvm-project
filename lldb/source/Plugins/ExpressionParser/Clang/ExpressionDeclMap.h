//===-- ExpressionDeclMap.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_EXPRESSIONDECLMAP_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_EXPRESSIONDECLMAP_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TaggedASTType.h"
#include "lldb/lldb-forward.h"
#include "lldb/lldb-types.h"

#include "clang/AST/ExternalASTSource.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/IntrusiveRefCntPtr.h"

namespace clang {
class ASTConsumer;
class NamedDecl;
} // namespace clang

namespace llvm {
class Value;
}

namespace lldb_private {

class DiagnosticManager;
class ExecutionContext;
class Materializer;

/// Abstract interface that the Clang expression parser and IRForTarget use to
/// resolve external entities, lay out the materialization struct and report
/// results.
///
/// This decouples those components from any concrete implementation. The
/// TypeSystemClang-backed ClangExpressionDeclMap implements it for the legacy
/// path; ClikeExpressionDeclMap implements it for TypeSystemClike (which has no
/// Clang AST) without depending on ClangExpressionDeclMap or TypeSystemClang.
class ExpressionDeclMap {
public:
  virtual ~ExpressionDeclMap() = default;

  /// Enable the state needed for parsing and IR transformation.
  virtual bool WillParse(ExecutionContext &exe_ctx,
                         Materializer *materializer) = 0;

  /// Disable the state needed for parsing and IR transformation.
  virtual void DidParse() = 0;

  virtual void InstallCodeGenerator(clang::ASTConsumer *code_gen) = 0;
  virtual void InstallDiagnosticManager(DiagnosticManager &diag_manager) = 0;
  virtual void SetLookupsEnabled(bool enabled) = 0;

  /// Create a clang::ExternalASTSource proxy for the parser's ASTContext.
  virtual llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> CreateProxy() = 0;

  /// [Used by IRForTarget] Wrap a QualType from the parser's ASTContext into a
  /// CompilerType. The legacy path wraps it in the parser's TypeSystemClang;
  /// the TypeSystemClike path maps it back onto a TypeSystemClike type.
  virtual CompilerType WrapType(clang::QualType qt) = 0;

  /// [Used by IRForTarget] Add a persistent variable (including the result).
  virtual bool AddPersistentVariable(const clang::NamedDecl *decl,
                                     ConstString name, TypeFromParser type,
                                     bool is_result, bool is_lvalue) = 0;

  /// [Used by IRForTarget] Add a variable to the materialization struct.
  virtual bool AddValueToStruct(const clang::NamedDecl *decl, ConstString name,
                                llvm::Value *value, size_t size,
                                lldb::offset_t alignment) = 0;

  /// [Used by IRForTarget] Finalize the struct layout.
  virtual bool DoStructLayout() = 0;

  /// [Used by IRForTarget] General information about the laid-out struct.
  virtual bool GetStructInfo(uint32_t &num_elements, size_t &size,
                             lldb::offset_t &alignment) = 0;

  /// [Used by IRForTarget] Information about one struct element.
  virtual bool GetStructElement(const clang::NamedDecl *&decl,
                                llvm::Value *&value, lldb::offset_t &offset,
                                ConstString &name, uint32_t index) = 0;

  /// [Used by IRForTarget] Resolve a symbol address by name.
  virtual lldb::addr_t GetSymbolAddress(ConstString name,
                                        lldb::SymbolType symbol_type) = 0;

  /// True for the TypeSystemClike-backed implementation (ClikeExpressionDeclMap).
  /// Used by ClangExpressionParser to install the parser's ASTContext with the
  /// right concrete type.
  virtual bool IsClikeDeclMap() const { return false; }
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_EXPRESSIONDECLMAP_H
