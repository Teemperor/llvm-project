//===-- CppExpressionDeclMap.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPEXPRESSIONDECLMAP_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPEXPRESSIONDECLMAP_H

#include "ClangASTGenerator.h"
#include "ClangExpressionVariable.h"
#include "ExpressionDeclMap.h"

#include "lldb/Core/Value.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/lldb-public.h"

#include <memory>
#include <optional>

namespace clang {
class ASTContext;
class DeclContext;
class DeclarationName;
class NamedDecl;
class RecordDecl;
class TagDecl;
} // namespace clang

namespace lldb_private {

class PersistentExpressionState;

/// Resolves external entities for a Clang-parsed expression out of the
/// module-level TypeSystemCpp (which has no Clang AST), and lays out the
/// materialization struct / reports results.
///
/// This is the TypeSystemCpp counterpart of ClangExpressionDeclMap. It does not
/// inherit from it and never references TypeSystemClang: it acts as a
/// clang::ExternalASTSource (through a proxy), synthesizes the Clang types the
/// parser needs via ClangASTGenerator (writing into the parser's raw
/// clang::ASTContext), and maps the expression's result type back onto a
/// TypeSystemCpp type stored in the scratch TypeSystemCpp.
class CppExpressionDeclMap : public ExpressionDeclMap {
public:
  CppExpressionDeclMap(bool keep_result_in_memory,
                       Materializer::PersistentVariableDelegate *result_delegate,
                       const lldb::TargetSP &target, ValueObject *ctx_obj);
  ~CppExpressionDeclMap() override;

  // ExpressionDeclMap
  bool WillParse(ExecutionContext &exe_ctx, Materializer *materializer) override;
  void DidParse() override;
  void InstallCodeGenerator(clang::ASTConsumer *code_gen) override;
  void InstallDiagnosticManager(DiagnosticManager &diag_manager) override;
  void SetLookupsEnabled(bool enabled) override { m_lookups_enabled = enabled; }
  llvm::IntrusiveRefCntPtr<clang::ExternalASTSource> CreateProxy() override;
  CompilerType WrapType(clang::QualType qt) override;
  bool AddPersistentVariable(const clang::NamedDecl *decl, ConstString name,
                             TypeFromParser type, bool is_result,
                             bool is_lvalue) override;
  bool AddValueToStruct(const clang::NamedDecl *decl, ConstString name,
                        llvm::Value *value, size_t size,
                        lldb::offset_t alignment) override;
  bool DoStructLayout() override;
  bool GetStructInfo(uint32_t &num_elements, size_t &size,
                     lldb::offset_t &alignment) override;
  bool GetStructElement(const clang::NamedDecl *&decl, llvm::Value *&value,
                        lldb::offset_t &offset, ConstString &name,
                        uint32_t index) override;
  lldb::addr_t GetSymbolAddress(ConstString name,
                                lldb::SymbolType symbol_type) override;
  bool IsCppDeclMap() const override { return true; }

  /// Give the map the parser's clang::ASTContext (called from
  /// ClangExpressionParser::ParseInternal).
  void InstallASTContext(clang::ASTContext &ast);

  // Called by the ExternalASTSource proxy (see CreateProxy).
  bool FindExternalVisibleDecls(const clang::DeclContext *dc,
                                clang::DeclarationName name,
                                llvm::SmallVectorImpl<clang::NamedDecl *> &decls);
  void CompleteType(clang::TagDecl *tag_decl);
  bool LayoutRecordType(
      const clang::RecordDecl *record, uint64_t &size, uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets);
  void StartTranslationUnit();

private:
  ClangASTGenerator &GetGenerator();
  uint64_t GetParserID() const { return (uint64_t)this; }

  /// Look up a local variable by name in the current frame and create a
  /// reference-typed VarDecl for it in \p dc.
  bool LookupLocalVariable(const clang::DeclContext *dc, ConstString name,
                           llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Create the synthetic `$__lldb_local_vars` namespace.
  clang::NamedDecl *
  CreateLocalVarsNamespace(const clang::DeclContext *dc,
                           llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Provide `$__lldb_class` (the type of the enclosing method's object) so an
  /// expression evaluated in a member function can use `this` and reach members
  /// unqualified. Mirrors ClangExpressionDeclMap::LookUpLldbClass' `this`-based
  /// path.
  void LookUpLldbClass(clang::DeclarationName name,
                       llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  const lldb::TargetSP m_target;
  ValueObject *m_ctx_obj;
  Materializer::PersistentVariableDelegate *m_result_delegate;
  bool m_keep_result_in_memory;

  clang::ASTContext *m_ast_context = nullptr;
  std::optional<ClangASTGenerator> m_generator;

  bool m_lookups_enabled = false;
  ExecutionContext m_exe_ctx;
  Materializer *m_materializer = nullptr;
  clang::ASTConsumer *m_code_gen = nullptr;
  DiagnosticManager *m_diagnostics = nullptr;
  PersistentExpressionState *m_persistent_vars = nullptr;
  lldb::ByteOrder m_byte_order = lldb::eByteOrderInvalid;
  uint32_t m_addr_byte_size = 0;

  /// All entities that were looked up for the parser.
  ExpressionVariableList m_found_entities;
  /// All entities that need to be placed in the materialization struct.
  ExpressionVariableList m_struct_members;

  lldb::offset_t m_struct_alignment = 0;
  size_t m_struct_size = 0;
  bool m_struct_laid_out = false;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPEXPRESSIONDECLMAP_H
