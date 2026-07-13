//===-- CppExpressionDeclMap.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CppExpressionDeclMap.h"

#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/ValueObject/ValueObjectVariable.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"

using namespace lldb_private;
using namespace lldb;

/// clang::ExternalASTSource proxy that forwards to a CppExpressionDeclMap.
/// Mirrors ClangASTSource's proxy: the parser's ASTContext owns the source, so
/// the state-holding decl map is kept separate.
namespace {
class CppASTSourceProxy : public clang::ExternalASTSource {
public:
  explicit CppASTSourceProxy(CppExpressionDeclMap &map) : m_map(map) {}

  bool FindExternalVisibleDeclsByName(
      const clang::DeclContext *dc, clang::DeclarationName name,
      const clang::DeclContext *original_dc) override {
    llvm::SmallVector<clang::NamedDecl *, 4> decls;
    m_map.FindExternalVisibleDecls(dc, name, decls);
    if (decls.empty()) {
      SetNoExternalVisibleDeclsForName(dc, name);
      return false;
    }
    SetExternalVisibleDeclsForName(dc, name, decls);
    return true;
  }

  void CompleteType(clang::TagDecl *tag_decl) override {
    m_map.CompleteType(tag_decl);
  }
  void CompleteType(clang::ObjCInterfaceDecl *) override {}

  bool layoutRecordType(
      const clang::RecordDecl *record, uint64_t &size, uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets) override {
    return m_map.LayoutRecordType(record, size, alignment, field_offsets,
                                  base_offsets, vbase_offsets);
  }

  void StartTranslationUnit(clang::ASTConsumer *) override {
    m_map.StartTranslationUnit();
  }

private:
  CppExpressionDeclMap &m_map;
};
} // namespace

CppExpressionDeclMap::CppExpressionDeclMap(
    bool keep_result_in_memory,
    Materializer::PersistentVariableDelegate *result_delegate,
    const lldb::TargetSP &target, ValueObject *ctx_obj)
    : m_target(target), m_ctx_obj(ctx_obj), m_result_delegate(result_delegate),
      m_keep_result_in_memory(keep_result_in_memory) {}

CppExpressionDeclMap::~CppExpressionDeclMap() = default;

ClangASTGenerator &CppExpressionDeclMap::GetGenerator() {
  assert(m_ast_context && "parser AST context not installed yet");
  if (!m_generator)
    m_generator.emplace(*m_ast_context);
  return *m_generator;
}

void CppExpressionDeclMap::InstallASTContext(clang::ASTContext &ast) {
  m_ast_context = &ast;
}

/// The scratch TypeSystemCpp that owns result/persistent types.
static TypeSystemCpp *GetScratchCpp(Target *target) {
  if (!target)
    return nullptr;
  auto ts_or_err =
      target->GetScratchTypeSystemForLanguage(eLanguageTypeC_plus_plus);
  if (!ts_or_err) {
    llvm::consumeError(ts_or_err.takeError());
    return nullptr;
  }
  return llvm::dyn_cast_or_null<TypeSystemCpp>(ts_or_err->get());
}

bool CppExpressionDeclMap::WillParse(ExecutionContext &exe_ctx,
                                     Materializer *materializer) {
  m_exe_ctx = exe_ctx;
  m_materializer = materializer;

  Target *target = exe_ctx.GetTargetPtr();
  if (target) {
    m_persistent_vars =
        target->GetPersistentExpressionStateForLanguage(eLanguageTypeC);
    Process *process = exe_ctx.GetProcessPtr();
    if (process) {
      m_byte_order = process->GetByteOrder();
      m_addr_byte_size = process->GetAddressByteSize();
    } else {
      m_byte_order = target->GetArchitecture().GetByteOrder();
      m_addr_byte_size = target->GetArchitecture().GetAddressByteSize();
    }
  }
  return true;
}

void CppExpressionDeclMap::DidParse() {
  for (size_t i = 0, e = m_found_entities.GetSize(); i != e; ++i) {
    if (ExpressionVariableSP var_sp = m_found_entities.GetVariableAtIndex(i))
      llvm::cast<ClangExpressionVariable>(var_sp.get())
          ->DisableParserVars(GetParserID());
  }
}

void CppExpressionDeclMap::InstallCodeGenerator(clang::ASTConsumer *code_gen) {
  m_code_gen = code_gen;
}

void CppExpressionDeclMap::InstallDiagnosticManager(
    DiagnosticManager &diag_manager) {
  m_diagnostics = &diag_manager;
}

llvm::IntrusiveRefCntPtr<clang::ExternalASTSource>
CppExpressionDeclMap::CreateProxy() {
  return llvm::makeIntrusiveRefCnt<CppASTSourceProxy>(*this);
}

CompilerType CppExpressionDeclMap::WrapType(clang::QualType qt) {
  if (qt.isNull())
    return {};
  TypeSystemCpp *scratch = GetScratchCpp(m_exe_ctx.GetTargetPtr());
  if (!scratch)
    return {};
  return GetGenerator().MapClangTypeToCpp(qt, *scratch);
}

void CppExpressionDeclMap::StartTranslationUnit() {
  if (!m_ast_context)
    return;
  m_ast_context->getTranslationUnitDecl()->setHasExternalVisibleStorage();
  m_ast_context->getTranslationUnitDecl()->setHasExternalLexicalStorage();
}

void CppExpressionDeclMap::CompleteType(clang::TagDecl *tag_decl) {
  if (m_ast_context)
    GetGenerator().CompleteRecord(tag_decl);
}

bool CppExpressionDeclMap::LayoutRecordType(
    const clang::RecordDecl *record, uint64_t &size, uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &vbase_offsets) {
  if (!m_ast_context)
    return false;
  return GetGenerator().LayoutRecord(record, size, alignment, field_offsets,
                                     base_offsets, vbase_offsets);
}

clang::NamedDecl *CppExpressionDeclMap::CreateLocalVarsNamespace(
    const clang::DeclContext *dc,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  clang::ASTContext &ast = *m_ast_context;
  auto *tu = ast.getTranslationUnitDecl();
  auto *nsd = clang::NamespaceDecl::Create(
      ast, tu, /*Inline=*/false, clang::SourceLocation(),
      clang::SourceLocation(), &ast.Idents.get("$__lldb_local_vars"),
      /*PrevDecl=*/nullptr, /*Nested=*/false);
  // Members (the local variables) are provided on demand.
  clang::Decl::castToDeclContext(nsd)->setHasExternalVisibleStorage(true);
  decls.push_back(nsd);
  return nsd;
}

bool CppExpressionDeclMap::FindExternalVisibleDecls(
    const clang::DeclContext *dc, clang::DeclarationName name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  if (!m_ast_context)
    return false;
  clang::IdentifierInfo *ii = name.getAsIdentifierInfo();
  if (!ii)
    return false;
  llvm::StringRef sname = ii->getName();

  if (const auto *nsd = llvm::dyn_cast<clang::NamespaceDecl>(dc)) {
    if (nsd->getName() == "$__lldb_local_vars")
      return LookupLocalVariable(dc, ConstString(sname), decls);
    return false;
  }

  if (llvm::isa<clang::TranslationUnitDecl>(dc)) {
    if (sname == "$__lldb_class") {
      LookUpLldbClass(name, decls);
      return !decls.empty();
    }
    if (sname == "$__lldb_local_vars") {
      CreateLocalVarsNamespace(dc, decls);
      return !decls.empty();
    }
    // A free function referenced by the expression (e.g. `globalFuncCall()`).
    if (!sname.starts_with("$"))
      return LookupFunctions(ConstString(sname), decls);
  }
  return false;
}

bool CppExpressionDeclMap::LookupFunctions(
    ConstString name, llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  SymbolContextList sc_list;
  ModuleFunctionSearchOptions options;
  options.include_inlines = false;
  options.include_symbols = true;
  target->GetImages().FindFunctions(
      name, lldb::eFunctionNameTypeFull | lldb::eFunctionNameTypeBase, options,
      sc_list);

  bool added = false;
  for (const SymbolContext &sc : sc_list) {
    Function *function = sc.function;
    if (!function)
      continue;
    Type *func_type = function->GetType();
    if (!func_type)
      continue;
    CompilerType func_cpp_type = func_type->GetFullCompilerType();
    if (!func_cpp_type || !func_cpp_type.GetTypeSystem<TypeSystemCpp>())
      continue;

    // Build the asm label the JIT resolves the call through.
    ConstString mangled = function->GetMangled().GetMangledName();
    if (!mangled)
      mangled = function->GetMangled().GetDemangledName();
    lldb::user_id_t module_id =
        sc.module_sp ? sc.module_sp->GetID() : LLDB_INVALID_UID;
    std::string label =
        FunctionCallLabel{/*discriminator=*/{}, module_id, function->GetID(),
                          mangled.GetStringRef()}
            .toString();

    if (clang::FunctionDecl *fd = GetGenerator().GenerateFunction(
            name.GetStringRef(), func_cpp_type, label)) {
      decls.push_back(fd);
      added = true;
    }
  }
  return added;
}

void CppExpressionDeclMap::LookUpLldbClass(
    clang::DeclarationName name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  StackFrame *frame = m_exe_ctx.GetFramePtr();
  if (!frame)
    return;

  // The enclosing method's object type is the pointee of the frame's `this`.
  VariableListSP vars = frame->GetInScopeVariableList(true);
  VariableSP this_var =
      vars ? vars->FindVariable(ConstString("this")) : VariableSP();
  if (!this_var)
    return;
  Type *this_type = this_var->GetType();
  if (!this_type)
    return;
  CompilerType class_cpp_type =
      this_type->GetForwardCompilerType().GetPointeeType();
  if (!class_cpp_type || !class_cpp_type.GetTypeSystem<TypeSystemCpp>())
    return;

  clang::QualType class_qt = GetGenerator().Generate(class_cpp_type);
  if (class_qt.isNull())
    return;
  auto *record = class_qt->getAsCXXRecordDecl();
  if (!record)
    return;
  // Make sure members are available for unqualified lookup.
  GetGenerator().CompleteRecord(record);

  clang::ASTContext &ast = *m_ast_context;

  // Declare `void $__lldb_expr(void *)` in the class so the wrapper's
  // out-of-line `$__lldb_class::$__lldb_expr` definition has a matching
  // declaration (and thus an implicit `this`).
  clang::FunctionProtoType::ExtProtoInfo epi;
  clang::QualType param_types[] = {ast.VoidPtrTy};
  clang::QualType method_qt = ast.getFunctionType(ast.VoidTy, param_types, epi);
  auto *method =
      clang::CXXMethodDecl::CreateDeserialized(ast, clang::GlobalDeclID());
  method->setDeclContext(record);
  method->setDeclName(&ast.Idents.get("$__lldb_expr"));
  method->setType(method_qt);
  method->setStorageClass(clang::SC_None);
  method->setConstexprKind(clang::ConstexprSpecKind::Unspecified);
  method->setAccess(clang::AS_public);
  method->addAttr(clang::UsedAttr::CreateImplicit(ast));
  auto *param =
      clang::ParmVarDecl::CreateDeserialized(ast, clang::GlobalDeclID());
  param->setDeclContext(method);
  param->setType(ast.VoidPtrTy);
  param->setStorageClass(clang::SC_None);
  method->setParams({param});
  record->addDecl(method);

  // Provide the `$__lldb_class` typedef the wrapper names.
  clang::TypeSourceInfo *ti = ast.getTrivialTypeSourceInfo(class_qt);
  auto *td = clang::TypedefDecl::Create(
      ast, ast.getTranslationUnitDecl(), clang::SourceLocation(),
      clang::SourceLocation(), name.getAsIdentifierInfo(), ti);
  decls.push_back(td);
}

bool CppExpressionDeclMap::LookupLocalVariable(
    const clang::DeclContext *dc, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Log *log = GetLog(LLDBLog::Expressions);
  StackFrame *frame = m_exe_ctx.GetFramePtr();
  if (!frame)
    return false;

  VariableListSP vars = frame->GetInScopeVariableList(true);
  if (!vars)
    return false;

  for (size_t i = 0, e = vars->GetSize(); i != e; ++i) {
    VariableSP var = vars->GetVariableAtIndex(i);
    if (!var || var->GetName() != name)
      continue;

    Type *var_type = var->GetType();
    if (!var_type)
      return false;
    CompilerType var_cpp_type = var_type->GetFullCompilerType();
    if (!var_cpp_type)
      return false;

    clang::QualType qt = GetGenerator().Generate(var_cpp_type);
    if (qt.isNull()) {
      LLDB_LOG(log, "CppEDM: couldn't generate a parser type for {0}", name);
      return false;
    }

    // Locals are represented by a reference to their storage (matching the
    // legacy Clang path), unless the variable is itself a reference.
    clang::QualType var_qt =
        qt->isReferenceType() ? qt : m_ast_context->getLValueReferenceType(qt);

    auto *vd = clang::VarDecl::Create(
        *m_ast_context, const_cast<clang::DeclContext *>(dc),
        clang::SourceLocation(), clang::SourceLocation(),
        name.GetCString() ? &m_ast_context->Idents.get(name.GetStringRef())
                          : nullptr,
        var_qt, nullptr, clang::SC_Static);
    decls.push_back(vd);

    ValueObjectSP valobj = ValueObjectVariable::Create(frame, var);
    auto *entity = new ClangExpressionVariable(valobj);
    m_found_entities.AddNewlyConstructedVariable(entity);
    entity->EnableParserVars(GetParserID());
    ClangExpressionVariable::ParserVars *pv =
        entity->GetParserVars(GetParserID());
    pv->m_named_decl = vd;
    pv->m_llvm_value = nullptr;
    pv->m_lldb_var = var;
    if (var_qt->isReferenceType())
      entity->m_flags |= ClangExpressionVariable::EVTypeIsReference;
    return true;
  }
  return false;
}

bool CppExpressionDeclMap::AddPersistentVariable(const clang::NamedDecl *decl,
                                                 ConstString name,
                                                 TypeFromParser type,
                                                 bool is_result,
                                                 bool is_lvalue) {
  Log *log = GetLog(LLDBLog::Expressions);

  // Only the expression result needs materialization here. `type` was produced
  // by WrapType, so it is already a TypeSystemCpp type living in the scratch
  // TypeSystemCpp.
  if (!(is_result && m_materializer))
    return true;

  if (!type) {
    LLDB_LOG(log, "CppEDM: result type couldn't be mapped to a TypeSystemCpp "
                  "type");
    return false;
  }

  TypeFromUser user_type(type);
  Status err;
  uint32_t offset = m_materializer->AddResultVariable(
      user_type, is_lvalue, m_keep_result_in_memory, m_result_delegate, err);
  if (!err.Success()) {
    LLDB_LOG(log, "CppEDM: couldn't add result variable: {0}", err.AsCString());
    return false;
  }

  auto *var = new ClangExpressionVariable(
      m_exe_ctx.GetBestExecutionContextScope(), name, user_type, m_byte_order,
      m_addr_byte_size);
  m_found_entities.AddNewlyConstructedVariable(var);
  var->EnableParserVars(GetParserID());
  var->GetParserVars(GetParserID())->m_named_decl = decl;
  var->EnableJITVars(GetParserID());
  var->GetJITVars(GetParserID())->m_offset = offset;
  return true;
}

bool CppExpressionDeclMap::AddValueToStruct(const clang::NamedDecl *decl,
                                            ConstString name,
                                            llvm::Value *value, size_t size,
                                            lldb::offset_t alignment) {
  m_struct_laid_out = false;

  if (ClangExpressionVariable::FindVariableInList(m_struct_members, decl,
                                                  GetParserID()))
    return true;

  ClangExpressionVariable *var = ClangExpressionVariable::FindVariableInList(
      m_found_entities, decl, GetParserID());
  if (!var)
    return false;

  ClangExpressionVariable::ParserVars *parser_vars =
      var->GetParserVars(GetParserID());
  parser_vars->m_llvm_value = value;

  var->EnableJITVars(GetParserID());
  ClangExpressionVariable::JITVars *jit_vars = var->GetJITVars(GetParserID());
  jit_vars->m_alignment = alignment;
  jit_vars->m_size = size;

  m_struct_members.AddVariable(var->shared_from_this());

  if (m_materializer) {
    Status err;
    uint32_t member_offset = 0;
    if (parser_vars->m_lldb_var)
      member_offset = m_materializer->AddVariable(parser_vars->m_lldb_var, err);
    else if (parser_vars->m_lldb_valobj_provider)
      member_offset = m_materializer->AddValueObject(
          name, parser_vars->m_lldb_valobj_provider, err);
    if (!err.Success())
      return false;
    jit_vars->m_offset = member_offset;
  }
  return true;
}

bool CppExpressionDeclMap::DoStructLayout() {
  if (m_struct_laid_out)
    return true;
  if (!m_materializer)
    return false;
  m_struct_alignment = m_materializer->GetStructAlignment();
  m_struct_size = m_materializer->GetStructByteSize();
  m_struct_laid_out = true;
  return true;
}

bool CppExpressionDeclMap::GetStructInfo(uint32_t &num_elements, size_t &size,
                                         lldb::offset_t &alignment) {
  if (!m_struct_laid_out)
    return false;
  num_elements = m_struct_members.GetSize();
  size = m_struct_size;
  alignment = m_struct_alignment;
  return true;
}

bool CppExpressionDeclMap::GetStructElement(const clang::NamedDecl *&decl,
                                            llvm::Value *&value,
                                            lldb::offset_t &offset,
                                            ConstString &name,
                                            uint32_t index) {
  if (!m_struct_laid_out || index >= m_struct_members.GetSize())
    return false;

  ExpressionVariableSP member_sp(m_struct_members.GetVariableAtIndex(index));
  if (!member_sp)
    return false;

  auto *var = llvm::cast<ClangExpressionVariable>(member_sp.get());
  ClangExpressionVariable::ParserVars *parser_vars =
      var->GetParserVars(GetParserID());
  ClangExpressionVariable::JITVars *jit_vars = var->GetJITVars(GetParserID());
  if (!parser_vars || !jit_vars)
    return false;

  decl = parser_vars->m_named_decl;
  value = parser_vars->m_llvm_value;
  offset = jit_vars->m_offset;
  name = member_sp->GetName();
  return true;
}

lldb::addr_t CppExpressionDeclMap::GetSymbolAddress(ConstString name,
                                                    lldb::SymbolType type) {
  if (!m_target)
    return LLDB_INVALID_ADDRESS;

  SymbolContextList sc_list;
  m_target->GetImages().FindSymbolsWithNameAndType(name, type, sc_list);
  for (const SymbolContext &sc : sc_list) {
    if (sc.symbol) {
      Address addr = sc.symbol->GetAddress();
      lldb::addr_t load_addr = addr.GetLoadAddress(m_target.get());
      if (load_addr != LLDB_INVALID_ADDRESS)
        return load_addr;
    }
  }
  return LLDB_INVALID_ADDRESS;
}
