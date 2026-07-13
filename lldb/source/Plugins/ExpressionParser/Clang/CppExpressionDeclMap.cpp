//===-- CppExpressionDeclMap.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CppExpressionDeclMap.h"

#include "ClangExpressionVariable.h"
#include "ClangUtil.h"
#include "NameSearchContext.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/ValueObject/ValueObjectVariable.h"

#include "clang/AST/DeclCXX.h"

using namespace lldb_private;
using namespace lldb;

CppExpressionDeclMap::CppExpressionDeclMap(
    bool keep_result_in_memory,
    Materializer::PersistentVariableDelegate *result_delegate,
    const lldb::TargetSP &target,
    const std::shared_ptr<ClangASTImporter> &importer, ValueObject *ctx_obj,
    bool ignore_context_qualifiers)
    : ClangExpressionDeclMap(keep_result_in_memory, result_delegate, target,
                             importer, ctx_obj, ignore_context_qualifiers) {}

CppExpressionDeclMap::~CppExpressionDeclMap() = default;

ClangASTGenerator &CppExpressionDeclMap::GetGenerator() {
  assert(m_clang_ast_context && "parser AST context not installed yet");
  if (!m_generator)
    m_generator.emplace(*m_clang_ast_context);
  return *m_generator;
}

bool CppExpressionDeclMap::LookupLocalVariable(
    NameSearchContext &context, ConstString name, SymbolContext &sym_ctx,
    const CompilerDeclContext &namespace_decl) {
  StackFrame *frame = m_parser_vars->m_exe_ctx.GetFramePtr();
  if (!frame)
    return false;

  // Match the requested name against the frame's in-scope variables directly.
  // The base class relies on Clang-AST-backed DeclContext lookups, which don't
  // exist for TypeSystemCpp; a name match against the variable list is enough
  // to resolve local variables here.
  VariableListSP vars = frame->GetInScopeVariableList(true);
  if (!vars)
    return false;

  for (size_t i = 0, e = vars->GetSize(); i != e; ++i) {
    VariableSP var = vars->GetVariableAtIndex(i);
    if (!var || var->GetName() != name)
      continue;

    ValueObjectSP valobj = ValueObjectVariable::Create(frame, var);
    AddOneVariable(context, var, valobj);
    context.m_found_variable = true;
    return true;
  }

  return false;
}

void CppExpressionDeclMap::LookupLocalVarNamespace(
    SymbolContext &sym_ctx, NameSearchContext &name_context) {
  // The base implementation requires the frame's DeclContext to be backed by a
  // TypeSystemClang. With TypeSystemCpp the frame's types have no Clang AST, so
  // create the synthetic `$__lldb_local_vars` namespace directly in the
  // parser's Clang AST instead. Local-variable lookups into this namespace are
  // then served by LookupLocalVariable above.
  StackFrame *frame =
      m_parser_vars ? m_parser_vars->m_exe_ctx.GetFramePtr() : nullptr;
  if (!frame)
    return;

  clang::NamespaceDecl *namespace_decl =
      m_clang_ast_context->GetUniqueNamespaceDeclaration(
          "$__lldb_local_vars", nullptr, OptionalClangModuleID());
  if (!namespace_decl)
    return;

  name_context.AddNamedDecl(namespace_decl);
  clang::DeclContext *ctxt = clang::Decl::castToDeclContext(namespace_decl);
  ctxt->setHasExternalVisibleStorage(true);
  name_context.m_found_local_vars_nsp = true;
}

bool CppExpressionDeclMap::GetVariableValue(VariableSP &var,
                                            lldb_private::Value &var_location,
                                            TypeFromUser *user_type,
                                            TypeFromParser *parser_type) {
  Log *log = GetLog(LLDBLog::Expressions);

  Type *var_type = var->GetType();
  if (!var_type) {
    LLDB_LOG(log, "CppEDM: variable has no type");
    return false;
  }

  CompilerType var_cpp_type = var_type->GetFullCompilerType();
  if (!var_cpp_type) {
    LLDB_LOG(log, "CppEDM: variable has no CompilerType");
    return false;
  }

  // Fall back to the base implementation for non-TypeSystemCpp module types.
  if (!var_cpp_type.GetTypeSystem<TypeSystemCpp>())
    return ClangExpressionDeclMap::GetVariableValue(var, var_location,
                                                    user_type, parser_type);

  CompilerType parser_ct = GetGenerator().Generate(var_cpp_type);
  if (!parser_ct) {
    LLDB_LOG(log, "CppEDM: couldn't generate a parser type for variable {0}",
             var->GetName());
    return false;
  }

  if (parser_type)
    *parser_type = TypeFromParser(parser_ct);

  // The concrete runtime location is resolved by the Materializer from the
  // lldb Variable; the value here only needs to carry the parser type.
  if (var_location.GetContextType() == Value::ContextType::Invalid)
    var_location.SetCompilerType(parser_ct);

  if (user_type)
    *user_type = TypeFromUser(var_cpp_type);

  return true;
}

void CppExpressionDeclMap::CompleteType(clang::TagDecl *tag_decl) {
  if (m_clang_ast_context && GetGenerator().CompleteRecord(tag_decl))
    return;
  ClangExpressionDeclMap::CompleteType(tag_decl);
}

bool CppExpressionDeclMap::layoutRecordType(
    const clang::RecordDecl *record, uint64_t &size, uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &vbase_offsets) {
  if (m_clang_ast_context &&
      GetGenerator().LayoutRecord(record, size, alignment, field_offsets,
                                  base_offsets, vbase_offsets))
    return true;
  return ClangExpressionDeclMap::layoutRecordType(
      record, size, alignment, field_offsets, base_offsets, vbase_offsets);
}

bool CppExpressionDeclMap::AddPersistentVariable(const clang::NamedDecl *decl,
                                                 ConstString name,
                                                 TypeFromParser parser_type,
                                                 bool is_result,
                                                 bool is_lvalue) {
  Log *log = GetLog(LLDBLog::Expressions);

  // Only the expression-result variable needs special treatment: its type must
  // be mapped back onto a TypeSystemCpp type living in the scratch
  // TypeSystemCpp. Everything else defers to the base class.
  if (!(is_result && m_parser_vars && m_parser_vars->m_materializer))
    return ClangExpressionDeclMap::AddPersistentVariable(decl, name,
                                                         parser_type, is_result,
                                                         is_lvalue);

  ExecutionContext &exe_ctx = m_parser_vars->m_exe_ctx;
  Target *target = exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  // Get the scratch TypeSystemCpp that will own the result type.
  auto ts_or_err =
      target->GetScratchTypeSystemForLanguage(eLanguageTypeC_plus_plus);
  if (!ts_or_err) {
    llvm::consumeError(ts_or_err.takeError());
    return ClangExpressionDeclMap::AddPersistentVariable(
        decl, name, parser_type, is_result, is_lvalue);
  }
  auto *scratch_cpp = llvm::dyn_cast_or_null<TypeSystemCpp>(ts_or_err->get());
  if (!scratch_cpp)
    return ClangExpressionDeclMap::AddPersistentVariable(
        decl, name, parser_type, is_result, is_lvalue);

  clang::QualType parser_qt = ClangUtil::GetQualType(parser_type);
  TypeFromUser user_type(
      GetGenerator().MapClangTypeToCpp(parser_qt, *scratch_cpp));
  if (!user_type) {
    LLDB_LOG(log, "CppEDM: couldn't map result type back to a TypeSystemCpp "
                  "type; falling back to the Clang scratch context");
    return ClangExpressionDeclMap::AddPersistentVariable(
        decl, name, parser_type, is_result, is_lvalue);
  }

  Status err;
  uint32_t offset = m_parser_vars->m_materializer->AddResultVariable(
      user_type, is_lvalue, m_keep_result_in_memory, m_result_delegate, err);
  if (!err.Success()) {
    LLDB_LOG(log, "CppEDM: couldn't add result variable: {0}", err.AsCString());
    return false;
  }

  ClangExpressionVariable *var = new ClangExpressionVariable(
      exe_ctx.GetBestExecutionContextScope(), name, user_type,
      m_parser_vars->m_target_info.byte_order,
      m_parser_vars->m_target_info.address_byte_size);

  m_found_entities.AddNewlyConstructedVariable(var);

  var->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *parser_vars =
      var->GetParserVars(GetParserID());
  parser_vars->m_named_decl = decl;

  var->EnableJITVars(GetParserID());
  ClangExpressionVariable::JITVars *jit_vars = var->GetJITVars(GetParserID());
  jit_vars->m_offset = offset;

  return true;
}
