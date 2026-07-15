//===-- CppExpressionDeclMap.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CppExpressionDeclMap.h"

#include "ClangExpressionUtil.h"

#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
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
#include "clang/AST/DeclarationName.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"

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
      // Don't cache a negative result produced while we were mid-generation:
      // the global-function search is skipped then (it would just be
      // reconciling names of decls we are synthesizing), so caching "no decls"
      // could hide a function that the expression genuinely references later.
      if (!m_map.IsGeneratingDecls())
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
    const lldb::TargetSP &target, ValueObject *ctx_obj,
    bool ignore_context_qualifiers)
    : m_target(target), m_ctx_obj(ctx_obj), m_result_delegate(result_delegate),
      m_keep_result_in_memory(keep_result_in_memory),
      m_ignore_context_qualifiers(ignore_context_qualifiers) {}

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
  CompilerType mapped = GetGenerator().MapClangTypeToCpp(qt, *scratch);
  if (!mapped)
    return {};
  return mapped;
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

  // An overloaded operator (`operator==`, `operator+`, ...) is looked up by a
  // special DeclarationName that carries no identifier, so it never reaches the
  // identifier-based path below. Resolve it to the matching free operator
  // function(s) in the target so `a == b` / `operator==(a, b)` bind to them.
  if (name.getNameKind() ==
      clang::DeclarationName::CXXOperatorName) {
    // Only genuine references from the expression (not the reconciliation
    // lookups clang runs while we synthesize decls) should trigger the
    // whole-module function search; see the note below.
    if (IsGeneratingDecls())
      return false;
    if (llvm::isa<clang::TranslationUnitDecl>(dc))
      return LookupOperatorFunctions(dc, name, decls);
    return false;
  }

  clang::IdentifierInfo *ii = name.getAsIdentifierInfo();
  if (!ii)
    return false;
  llvm::StringRef sname = ii->getName();

  if (const auto *nsd = llvm::dyn_cast<clang::NamespaceDecl>(dc)) {
    if (nsd->getName() == "$__lldb_local_vars")
      return LookupLocalVariable(dc, ConstString(sname), decls);
    // A member lookup inside a real C++ namespace we created earlier.
    if (m_namespace_maps.count(nsd)) {
      // Skip the lookup while we are synthesizing decls: placing a
      // namespace-scoped record into its clang NamespaceDecl (which has
      // external visible storage) makes clang reconcile that name here, but
      // such a lookup is internal, not a reference from the expression.
      // Servicing it would generate another type into the same namespace and
      // recurse without end. Genuine references resolve while the parser runs
      // (outside generation).
      if (IsGeneratingDecls())
        return false;
      return LookupInNamespace(nsd, ConstString(sname), decls);
    }
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
    // A persistent expression variable ($0, $1, $foo, ...) referenced by the
    // expression, but not one of the internal $__lldb_* names.
    if (sname.starts_with("$") && !sname.starts_with("$__lldb"))
      return LookupPersistentVariable(dc, ConstString(sname), decls);
    // A free function or global variable referenced by the expression
    // (e.g. `globalFuncCall()` or `g_global`).
    if (!sname.starts_with("$")) {
      // Skip the whole-module function search while we are synthesizing decls:
      // adding a named decl to the (external-visible) TU makes clang look that
      // name up here, but those are internal reconciliation lookups, not
      // references from the expression. Running a module-wide FindFunctions per
      // such name is what made completing large types (e.g. clang::Sema)
      // effectively never finish. Genuine references are looked up while the
      // parser runs (outside generation) and still resolve.
      if (IsGeneratingDecls())
        return false;
      // Unqualified name: first honor the frame's enclosing namespace scope
      // (an expression in `A::B::f` should see names in `A::B`, then `A`), then
      // fall through to a global-scope search.
      if (LookupInFrameNamespaces(dc, ConstString(sname), decls))
        return true;
      // A name at file scope can refer to a global variable or a function.
      // Prefer the variable (an object shadows a function of the same name in
      // C/C++ unqualified lookup), then fall back to functions, then to types,
      // and finally to a namespace (so a qualified `A::B::x` can start
      // resolving from the leftmost namespace name).
      if (LookupGlobalVariable(dc, ConstString(sname), /*module=*/nullptr,
                               CompilerDeclContext(), decls))
        return true;
      if (LookupFunctions(dc, ConstString(sname), /*module=*/nullptr,
                          CompilerDeclContext(), decls))
        return true;
      if (LookupType(dc, ConstString(sname), /*module=*/nullptr,
                     CompilerDeclContext(), decls))
        return true;
      return LookupNamespace(dc, ConstString(sname), decls);
    }
  }
  return false;
}

bool CppExpressionDeclMap::LookupFunctions(
    const clang::DeclContext *dc, ConstString name, lldb::ModuleSP module,
    const CompilerDeclContext &scope,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  SymbolContextList sc_list;
  ModuleFunctionSearchOptions options;
  options.include_inlines = false;
  options.include_symbols = true;
  if (scope.IsValid() && module) {
    // A namespace-scoped lookup: restrict to functions declared in that
    // namespace, matching only the basename.
    module->FindFunctions(name, scope, lldb::eFunctionNameTypeBase, options,
                          sc_list);
  } else {
    target->GetImages().FindFunctions(
        name, lldb::eFunctionNameTypeFull | lldb::eFunctionNameTypeBase,
        options, sc_list);
  }

  clang::ASTContext &ast = *m_ast_context;
  bool added = false;
  // FindFunctions can return the same underlying function more than once (e.g.
  // matched both by base and full name, or once per module), and a name can
  // resolve to several distinct functions that share an identical signature
  // (e.g. a file-local `static` function defined in several translation units).
  // Both cases would synthesize identical-signature FunctionDecls, which clang
  // then reports as an ambiguous call. Genuine overloads differ in signature
  // and must remain distinct, so dedup by the function's type signature.
  llvm::StringSet<> seen_signatures;
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

    if (!seen_signatures.insert(func_cpp_type.GetTypeName().GetStringRef())
             .second)
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
      // A namespace-scoped function must live in the namespace decl so its
      // (mangled) qualified name is correct and clang finds it there.
      if (dc && dc != ast.getTranslationUnitDecl()) {
        fd->setDeclContext(const_cast<clang::DeclContext *>(dc));
        const_cast<clang::DeclContext *>(dc)->addDecl(fd);
      }
      decls.push_back(fd);
      added = true;
    }
  }
  return added;
}

bool CppExpressionDeclMap::LookupOperatorFunctions(
    const clang::DeclContext *dc, clang::DeclarationName name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  // The debug info / symbol table spells a free operator function as
  // `operator<X>` (e.g. `operator==`), so reconstruct that spelling from the
  // operator kind and search for it. A word-spelled operator (new/delete) needs
  // a separating space (`operator new`), while symbol operators do not
  // (`operator==`).
  clang::OverloadedOperatorKind oo = name.getCXXOverloadedOperator();
  const char *spelling = clang::getOperatorSpelling(oo);
  if (!spelling)
    return false;
  llvm::StringRef spelling_ref(spelling);
  const char *sep =
      (!spelling_ref.empty() && llvm::isAlpha(spelling_ref.front())) ? " " : "";
  ConstString search_name(
      (llvm::Twine("operator") + sep + spelling_ref).str());

  SymbolContextList sc_list;
  ModuleFunctionSearchOptions options;
  options.include_inlines = false;
  options.include_symbols = true;
  target->GetImages().FindFunctions(
      search_name, lldb::eFunctionNameTypeFull | lldb::eFunctionNameTypeBase,
      options, sc_list);

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

    // Name the generated decl with the operator DeclarationName so operator
    // syntax (`a == b`) and the explicit `operator==(a, b)` spelling both bind
    // to it during overload resolution.
    if (clang::FunctionDecl *fd =
            GetGenerator().GenerateFunction(name, func_cpp_type, label)) {
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

  // Inside a lambda that captured `this`, the frame's `this` is the (unnamed)
  // lambda closure object, whose captured `this` is a member named `this`
  // (DWARF) / `__this` (CodeView). Use the captured object's class as
  // `$__lldb_class` so unqualified member lookups resolve against the enclosing
  // class, mirroring ClangExpressionDeclMap::LookUpLldbClass and the captured-
  // `this` handling in ClangExpressionSourceCode.
  if (ValueObjectSP this_valobj = ValueObjectVariable::Create(frame, this_var)) {
    ValueObjectSP captured = this_valobj->GetChildMemberWithName("this");
    if (!captured)
      captured = this_valobj->GetChildMemberWithName("__this");
    if (captured) {
      CompilerType captured_pointee =
          captured->GetCompilerType().GetPointeeType();
      if (captured_pointee)
        class_cpp_type = captured_pointee;
    }
  }

  if (!class_cpp_type || !class_cpp_type.GetTypeSystem<TypeSystemCpp>())
    return;

  // A cv-qualified method (e.g. `int f() const`) has a `this` whose pointee is
  // a `const`/`volatile` cpp type. The injected `$__lldb_expr` method below
  // must carry the same cv-qualifiers, otherwise its implicit `this` is a
  // plain `T*` and member reads pick the wrong (non-const) overload / writes to
  // const members are wrongly allowed. Read the cv-qualifiers off the pointee
  // and strip them so the class record itself is generated unqualified.
  bool this_is_const = false;
  bool this_is_volatile = false;
  {
    // DWARF nests one qualifier per node, so `const volatile T` is a stack of
    // CVQualifiedType nodes; walk the whole chain to collect the flags and to
    // reach the unqualified record used for the class type itself.
    unsigned quals = class_cpp_type.GetTypeQualifiers();
    // `--c++-ignore-context-qualifiers` drops the method's cv-qualifiers so
    // members can be mutated; the wrapper's out-of-line definition is then
    // emitted unqualified too (see ClangExpressionSourceCode), so keep them in
    // sync here.
    if (!m_ignore_context_qualifiers) {
      this_is_const = (quals & 0x1) != 0;
      this_is_volatile = (quals & 0x4) != 0;
    }
    auto ts = class_cpp_type.GetTypeSystem<TypeSystemCpp>();
    while (auto *cv = llvm::dyn_cast<cpp_typesystem::CVQualifiedType>(
               TypeSystemCpp::GetCppType(class_cpp_type.GetOpaqueQualType()))) {
      if (!ts)
        break;
      class_cpp_type = ts->GetCompilerType(cv->GetUnderlyingType());
    }
  }
  if (!class_cpp_type)
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
  // declaration (and thus an implicit `this`). Mirror the cv-qualifiers of the
  // frame's actual method so the implicit `this` is `const`/`volatile` too.
  clang::FunctionProtoType::ExtProtoInfo epi;
  if (this_is_const || this_is_volatile) {
    clang::Qualifiers quals = epi.TypeQuals;
    if (this_is_const)
      quals.addConst();
    if (this_is_volatile)
      quals.addVolatile();
    epi.TypeQuals = quals;
  }
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

  // No real local variable matched. When evaluating inside a lambda, its
  // captures are surfaced as local variables too (the wrapper emits
  // `using $__lldb_local_vars::<capture>;`). A capture is a member of the
  // lambda closure object, so look it up there and bind the decl to a
  // ValueObject provider that re-fetches the capture for the current frame
  // (mirroring ClangExpressionDeclMap::LookupLocalVariable).
  auto find_capture = [](ConstString varname, StackFrame *f) -> ValueObjectSP {
    if (auto lambda = ClangExpressionUtil::GetLambdaValueObject(f))
      return lambda->GetChildMemberWithName(varname);
    return nullptr;
  };
  if (ValueObjectSP capture = find_capture(name, frame)) {
    CompilerType capture_type = capture->GetCompilerType();
    if (!capture_type || !capture_type.GetTypeSystem<TypeSystemCpp>())
      return false;
    clang::QualType qt = GetGenerator().Generate(capture_type);
    if (qt.isNull()) {
      LLDB_LOG(log, "CppEDM: couldn't generate a parser type for capture {0}",
               name);
      return false;
    }
    clang::QualType var_qt =
        qt->isReferenceType() ? qt : m_ast_context->getLValueReferenceType(qt);
    auto *vd = clang::VarDecl::Create(
        *m_ast_context, const_cast<clang::DeclContext *>(dc),
        clang::SourceLocation(), clang::SourceLocation(),
        &m_ast_context->Idents.get(name.GetStringRef()), var_qt, nullptr,
        clang::SC_Static);
    decls.push_back(vd);

    auto *entity = new ClangExpressionVariable(capture);
    m_found_entities.AddNewlyConstructedVariable(entity);
    entity->EnableParserVars(GetParserID());
    ClangExpressionVariable::ParserVars *pv =
        entity->GetParserVars(GetParserID());
    pv->m_named_decl = vd;
    pv->m_llvm_value = nullptr;
    pv->m_lldb_valobj_provider = std::move(find_capture);
    if (var_qt->isReferenceType())
      entity->m_flags |= ClangExpressionVariable::EVTypeIsReference;
    return true;
  }
  return false;
}

bool CppExpressionDeclMap::LookupGlobalVariable(
    const clang::DeclContext *dc, ConstString name, lldb::ModuleSP module,
    const CompilerDeclContext &scope,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Log *log = GetLog(LLDBLog::Expressions);
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  // Search for a global (or file-static) variable by name, either restricted to
  // a namespace (in one module) or across the whole target.
  VariableList vars;
  if (scope.IsValid() && module)
    module->FindGlobalVariables(name, scope, -1, vars);
  else
    target->GetImages().FindGlobalVariables(name, -1, vars);

  for (size_t i = 0, e = vars.GetSize(); i != e; ++i) {
    VariableSP var = vars.GetVariableAtIndex(i);
    // A namespace-scoped variable reports its fully-qualified name (e.g.
    // "A::B::j") from GetName(); compare the unqualified spelling.
    if (!var || (var->GetName() != name && var->GetUnqualifiedName() != name))
      continue;

    // An unqualified name looked up at global scope (no namespace `scope`)
    // resolves to a variable at global scope only; a namespace-scoped variable
    // (e.g. `NN::a`) must not shadow the true global (`::a`). Target-wide
    // FindGlobalVariables does not filter by scope, so `NN::a` and `::a` both
    // come back for `a`; reject the ones that live in a (non-global-reachable)
    // namespace here so `::a` wins, matching C++ unqualified lookup. A scoped
    // lookup (`scope` valid) already restricted the search to one namespace.
    if (!scope.IsValid()) {
      // A static class member (e.g. `A::a`) is emitted like a global (its
      // enclosing record is not modelled as a namespace decl context, so it
      // reports an invalid/global decl context), but it is not reachable from
      // the global scope by an unqualified name or by `::a`. It is found
      // instead via record member lookup when the expression writes `A::a`.
      // Reject it here so a true global `::a` is not shadowed by a same-named
      // class static.
      if (var->IsStaticMember())
        continue;
      CompilerDeclContext var_ctx = var->GetDeclContext();
      if (var_ctx.IsValid()) {
        auto *ns = static_cast<const cpp_typesystem::Namespace *>(
            var_ctx.GetOpaqueDeclContext());
        bool global_reachable = true;
        for (; ns; ns = ns->GetParent()) {
          if (!ns->IsInline() && !ns->GetName().GetName().empty()) {
            global_reachable = false;
            break;
          }
        }
        if (!global_reachable)
          continue;
      }
    }

    Type *var_type = var->GetType();
    if (!var_type)
      continue;
    CompilerType var_cpp_type = var_type->GetFullCompilerType();
    // Only handle variables whose type is described by a TypeSystemCpp; other
    // language plugins own their own decl-map path.
    if (!var_cpp_type || !var_cpp_type.GetTypeSystem<TypeSystemCpp>())
      continue;

    clang::QualType qt = GetGenerator().Generate(var_cpp_type);
    if (qt.isNull()) {
      LLDB_LOG(log, "CppEDM: couldn't generate a parser type for global {0}",
               name);
      continue;
    }

    // Like locals, globals are represented by a reference to their storage
    // (unless the variable is itself a reference), so the materializer can bind
    // the decl to the variable's load address.
    clang::QualType var_qt =
        qt->isReferenceType() ? qt : m_ast_context->getLValueReferenceType(qt);

    auto *vd = clang::VarDecl::Create(
        *m_ast_context, const_cast<clang::DeclContext *>(dc),
        clang::SourceLocation(), clang::SourceLocation(),
        &m_ast_context->Idents.get(name.GetStringRef()), var_qt, nullptr,
        clang::SC_Static);
    decls.push_back(vd);

    ValueObjectSP valobj = ValueObjectVariable::Create(
        m_exe_ctx.GetBestExecutionContextScope(), var);
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

bool CppExpressionDeclMap::LookupType(
    const clang::DeclContext *dc, ConstString name, lldb::ModuleSP module,
    const CompilerDeclContext &scope,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Log *log = GetLog(LLDBLog::Expressions);
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  // Search for a type with this (unqualified) name. The legacy
  // ClangExpressionDeclMap gets type lookups from ClangASTSource; the
  // TypeSystemCpp path has no such helper, so do it here. A namespace-scoped
  // lookup restricts the query to the namespace (in one module).
  TypeResults results;
  if (scope.IsValid() && module) {
    TypeQuery query(scope, name, TypeQueryOptions::e_find_one);
    module->FindTypes(query, results);
  } else {
    TypeQuery query(name.GetStringRef(), TypeQueryOptions::e_exact_match |
                                             TypeQueryOptions::e_find_one);
    target->GetImages().FindTypes(nullptr, query, results);
  }
  lldb::TypeSP type_sp = results.GetFirstType();
  if (!type_sp)
    return false;

  CompilerType type = type_sp->GetFullCompilerType();
  // Only handle types owned by a TypeSystemCpp; other language plugins own
  // their own decl-map path.
  if (!type || !type.GetTypeSystem<TypeSystemCpp>())
    return false;

  clang::QualType qt = GetGenerator().Generate(type);
  if (qt.isNull()) {
    LLDB_LOG(log, "CppEDM: couldn't generate a parser type for type {0}", name);
    return false;
  }

  if (clang::TagDecl *tag = qt->getAsTagDecl()) {
    // Make sure the members/enumerators are available (e.g. for sizeof or
    // offsetof, which need a complete type).
    if (auto *record = llvm::dyn_cast<clang::RecordDecl>(tag))
      GetGenerator().CompleteRecord(record);
    decls.push_back(tag);
    return true;
  }
  if (const auto *tdt = qt->getAs<clang::TypedefType>()) {
    decls.push_back(tdt->getDecl());
    return true;
  }
  return false;
}

bool CppExpressionDeclMap::LookupNamespace(
    const clang::DeclContext *parent_dc, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target || !m_ast_context)
    return false;

  // Discover, per module, the namespace named `name` in the parent scope. For a
  // top-level lookup (parent_dc is the translation unit) the parent scope is
  // the global namespace (an invalid CompilerDeclContext); for a nested lookup
  // it is each entry of the parent namespace's cached NamespaceMap. This
  // mirrors ClangASTSource's namespace-map machinery, but produces a
  // TypeSystemCpp-backed CompilerDeclContext.
  NamespaceMap map;

  auto is_global_reachable = [](const CompilerDeclContext &found) {
    // True if `found` is reachable from the global scope: a top-level
    // namespace, or one nested only inside transparent (anonymous/inline)
    // namespaces.
    for (auto *ns = static_cast<const cpp_typesystem::Namespace *>(
             found.GetOpaqueDeclContext())
                        ->GetParent();
         ns; ns = ns->GetParent()) {
      if (!ns->IsInline() && !ns->GetName().GetName().empty())
        return false;
    }
    return true;
  };

  auto add_from_module = [&](const lldb::ModuleSP &module_sp,
                             const CompilerDeclContext &parent_ctx,
                             bool top_level) {
    if (!module_sp)
      return;
    SymbolFile *sf = module_sp->GetSymbolFile();
    if (!sf)
      return;
    CompilerDeclContext found;
    if (top_level) {
      // Prefer a root (directly top-level) namespace so an unqualified `B`
      // never resolves to a nested `A::B` (only its parent `A` should). Fall
      // back to any global-reachable namespace (e.g. one inside an anonymous
      // namespace at file scope, like `InAnon1`).
      found = sf->FindNamespace(name, CompilerDeclContext(),
                                /*only_root_namespaces=*/true);
      if (!found.IsValid()) {
        CompilerDeclContext any = sf->FindNamespace(name, CompilerDeclContext());
        if (any.IsValid() &&
            llvm::isa_and_nonnull<TypeSystemCpp>(any.GetTypeSystem()) &&
            is_global_reachable(any))
          found = any;
      }
    } else {
      found = sf->FindNamespace(name, parent_ctx);
    }
    // Only namespaces owned by a TypeSystemCpp participate in this path.
    if (found.IsValid() &&
        llvm::isa_and_nonnull<TypeSystemCpp>(found.GetTypeSystem()))
      map.emplace_back(module_sp, found);
  };

  const NamespaceMap *parent_map = nullptr;
  const auto *parent_nsd = llvm::dyn_cast<clang::NamespaceDecl>(parent_dc);
  if (parent_nsd) {
    auto it = m_namespace_maps.find(parent_nsd);
    if (it == m_namespace_maps.end())
      return false;
    parent_map = &it->second;
  }

  if (parent_map) {
    // Nested: look for `name` in each module's copy of the parent namespace.
    for (const auto &entry : *parent_map)
      add_from_module(entry.first, entry.second, /*top_level=*/false);
  } else {
    // Top level (parent is the global namespace): search every module.
    for (const lldb::ModuleSP &module_sp : target->GetImages().Modules())
      add_from_module(module_sp, CompilerDeclContext(), /*top_level=*/true);
  }

  if (map.empty())
    return false;

  // Create the parser-side namespace decl (once) and cache its map. The decl is
  // given external visible storage so clang calls back into us for member
  // lookups (LookupInNamespace).
  clang::ASTContext &ast = *m_ast_context;
  auto *nsd = clang::NamespaceDecl::Create(
      ast, const_cast<clang::DeclContext *>(parent_dc), /*Inline=*/false,
      clang::SourceLocation(), clang::SourceLocation(),
      &ast.Idents.get(name.GetStringRef()), /*PrevDecl=*/nullptr,
      /*Nested=*/false);
  clang::Decl::castToDeclContext(nsd)->setHasExternalVisibleStorage(true);
  const_cast<clang::DeclContext *>(parent_dc)->addDecl(nsd);

  // Register the cpp namespace -> clang namespace mapping so types declared in
  // this namespace are generated inside it (correct qualified mangling). Use
  // the first module's decl context as the representative cpp namespace.
  auto *cpp_ns = static_cast<const cpp_typesystem::Namespace *>(
      map.front().second.GetOpaqueDeclContext());
  GetGenerator().RegisterNamespace(cpp_ns, nsd);

  m_namespace_maps.insert({nsd, std::move(map)});
  decls.push_back(nsd);
  return true;
}

bool CppExpressionDeclMap::LookupInNamespace(
    const clang::NamespaceDecl *nsd, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  auto it = m_namespace_maps.find(nsd);
  if (it == m_namespace_maps.end())
    return false;
  const NamespaceMap &map = it->second;
  auto *dc = const_cast<clang::NamespaceDecl *>(nsd);

  // A name inside a namespace can be a nested namespace, a type, a variable or
  // a function. Try a nested namespace first (so a qualified `A::B::x` keeps
  // descending), then the value/type entities. Fan the search out over every
  // module that has this namespace.
  if (LookupNamespace(dc, name, decls))
    return true;

  bool added = false;
  for (const auto &entry : map) {
    const lldb::ModuleSP &module_sp = entry.first;
    const CompilerDeclContext &scope = entry.second;
    // Prefer a variable, then a function, then a type (matching the TU-scope
    // ordering), but collect across modules.
    if (LookupGlobalVariable(dc, name, module_sp, scope, decls)) {
      added = true;
      break;
    }
    if (LookupFunctions(dc, name, module_sp, scope, decls))
      added = true;
    if (!added && LookupType(dc, name, module_sp, scope, decls)) {
      added = true;
      break;
    }
  }
  return added;
}

bool CppExpressionDeclMap::LookupInFrameNamespaces(
    const clang::DeclContext *dc, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  StackFrame *frame = m_exe_ctx.GetFramePtr();
  if (!frame)
    return false;

  SymbolContext sc = frame->GetSymbolContext(lldb::eSymbolContextFunction |
                                             lldb::eSymbolContextBlock);
  Block *function_block = sc.GetFunctionBlock();
  if (!function_block || !sc.module_sp)
    return false;
  lldb::ModuleSP module = sc.module_sp;

  // The frame's TypeSystemCpp (owns the namespaces of the module the frame is
  // in). Used both to resolve the frame's own namespace scope and its active
  // using-directives.
  auto ts_or_err =
      module->GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (!ts_or_err) {
    llvm::consumeError(ts_or_err.takeError());
    return false;
  }
  auto *ts = llvm::dyn_cast_or_null<TypeSystemCpp>(ts_or_err->get());
  if (!ts)
    return false;

  // First honor the `using namespace` directives lexically in scope at the
  // current PC: a `using namespace ns2;` makes an unqualified `value` resolve
  // to `ns2::value`. Search the innermost block containing the PC (falling back
  // to the function block).
  Block *pc_block = sc.block ? sc.block : function_block;
  for (const CompilerDeclContext &used :
       ts->GetUsingDirectiveNamespaces(*pc_block)) {
    if (!used.IsValid())
      continue;
    if (LookupGlobalVariable(dc, name, module, used, decls))
      return true;
    if (LookupFunctions(dc, name, module, used, decls))
      return true;
    if (LookupType(dc, name, module, used, decls))
      return true;
  }

  // Then honor the frame's enclosing namespace scope: walk the namespaces the
  // frame function is declared in, innermost-first, doing a scoped lookup in
  // each. Members of the frame's own namespace shadow those of its parents. A
  // global-scope function has no enclosing namespace (an invalid context).
  CompilerDeclContext frame_ctx = function_block->GetDeclContext();
  if (!frame_ctx ||
      !llvm::isa_and_nonnull<TypeSystemCpp>(frame_ctx.GetTypeSystem()))
    return false;
  for (auto *ns = static_cast<const cpp_typesystem::Namespace *>(
           frame_ctx.GetOpaqueDeclContext());
       ns; ns = ns->GetParent()) {
    CompilerDeclContext scope(ts, const_cast<cpp_typesystem::Namespace *>(ns));
    if (LookupGlobalVariable(dc, name, module, scope, decls))
      return true;
    if (LookupFunctions(dc, name, module, scope, decls))
      return true;
    if (LookupType(dc, name, module, scope, decls))
      return true;
  }
  return false;
}

bool CppExpressionDeclMap::LookupPersistentVariable(
    const clang::DeclContext *dc, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Log *log = GetLog(LLDBLog::Expressions);
  if (!m_persistent_vars)
    return false;

  lldb::ExpressionVariableSP pvar_sp = m_persistent_vars->GetVariable(name);
  if (!pvar_sp)
    return false;
  auto *pvar = llvm::dyn_cast<ClangExpressionVariable>(pvar_sp.get());
  if (!pvar)
    return false;

  CompilerType pvar_type = pvar->GetCompilerType();
  if (!pvar_type || !pvar_type.GetTypeSystem<TypeSystemCpp>())
    return false;

  clang::QualType qt = GetGenerator().Generate(pvar_type);
  if (qt.isNull()) {
    LLDB_LOG(log, "CppEDM: couldn't generate a parser type for pvar {0}", name);
    return false;
  }

  // Persistent variables are referenced through their storage, so hand out a
  // reference-typed VarDecl (unless the variable is itself a reference).
  clang::QualType var_qt =
      qt->isReferenceType() ? qt : m_ast_context->getLValueReferenceType(qt);
  auto *vd = clang::VarDecl::Create(
      *m_ast_context, const_cast<clang::DeclContext *>(dc),
      clang::SourceLocation(), clang::SourceLocation(),
      &m_ast_context->Idents.get(name.GetStringRef()), var_qt, nullptr,
      clang::SC_Static);
  decls.push_back(vd);

  // Bind the persistent variable (which lives in the persistent state, not in
  // m_found_entities) to this decl so AddValueToStruct can materialize it.
  pvar->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *parser_vars =
      pvar->GetParserVars(GetParserID());
  parser_vars->m_named_decl = vd;
  parser_vars->m_llvm_value = nullptr;
  parser_vars->m_lldb_value.Clear();
  return true;
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
  // A persistent variable ($0, $foo, ...) lives in the persistent state rather
  // than in m_found_entities; look for it there.
  bool is_persistent_variable = false;
  if (!var && m_persistent_vars) {
    var = ClangExpressionVariable::FindVariableInList(*m_persistent_vars, decl,
                                                      GetParserID());
    is_persistent_variable = true;
  }
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
    if (is_persistent_variable) {
      lldb::ExpressionVariableSP var_sp(var->shared_from_this());
      member_offset =
          m_materializer->AddPersistentVariable(var_sp, nullptr, err);
    } else if (parser_vars->m_lldb_var)
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
