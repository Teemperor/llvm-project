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
#include "ClangExpressionDeclMap.h"

#include <memory>
#include <optional>

namespace lldb_private {

/// A ClangExpressionDeclMap that resolves external entities out of the
/// module-level TypeSystemCpp instead of a Clang AST.
///
/// It is installed (in place of the plain ClangExpressionDeclMap) when the
/// `symbols.enable-typesystem-cpp` setting is on, in which case debug-info
/// types are cpp_typesystem::Type nodes rather than clang::Decls. Because the
/// ASTImporter can only copy between Clang ASTs, the base class' type-copying
/// path can't be used; instead this class synthesizes the needed clang AST from
/// the cpp_typesystem description via a ClangASTGenerator (never using the
/// ASTImporter).
///
/// Everything else -- struct layout for IRForTarget, materialization, and the
/// expression-result plumbing -- is reused unchanged from the base class. This
/// subclass only overrides the handful of hooks that would otherwise reach for
/// a Clang-AST module type:
///   * GetVariableValue / LookupLocalVariable -- build the parser type from the
///     cpp type and match locals by name.
///   * CompleteType / layoutRecordType -- complete and lay out generated
///     records from their debug-info description.
///   * AddPersistentVariable -- map the expression's result type back onto a
///     TypeSystemCpp type stored in the scratch TypeSystemCpp.
class CppExpressionDeclMap : public ClangExpressionDeclMap {
public:
  CppExpressionDeclMap(
      bool keep_result_in_memory,
      Materializer::PersistentVariableDelegate *result_delegate,
      const lldb::TargetSP &target,
      const std::shared_ptr<ClangASTImporter> &importer, ValueObject *ctx_obj,
      bool ignore_context_qualifiers);

  ~CppExpressionDeclMap() override;

  bool AddPersistentVariable(const clang::NamedDecl *decl, ConstString name,
                             TypeFromParser type, bool is_result,
                             bool is_lvalue) override;

  void CompleteType(clang::TagDecl *tag_decl) override;

  bool layoutRecordType(
      const clang::RecordDecl *record, uint64_t &size, uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets) override;

protected:
  bool LookupLocalVariable(NameSearchContext &context, ConstString name,
                           SymbolContext &sym_ctx,
                           const CompilerDeclContext &namespace_decl) override;

  void LookupLocalVarNamespace(SymbolContext &sym_ctx,
                               NameSearchContext &name_context) override;

  bool GetVariableValue(lldb::VariableSP &var, lldb_private::Value &var_location,
                        TypeFromUser *found_type,
                        TypeFromParser *parser_type) override;

private:
  /// Lazily-created translator from cpp_typesystem types to the parser's clang
  /// AST. Created on first use, once the parser's TypeSystemClang is installed.
  ClangASTGenerator &GetGenerator();

  std::optional<ClangASTGenerator> m_generator;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CPPEXPRESSIONDECLMAP_H
