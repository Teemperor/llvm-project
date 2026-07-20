//===-- CppExpressionDeclMap.cpp ------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CppExpressionDeclMap.h"

#include "ClangExpressionUtil.h"
#include "ClangTypeConverter.h"

#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Expression/DiagnosticManager.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Expression/ExpressionVariable.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/Variable.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/RegisterContext.h"
#include "lldb/Target/StackFrame.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"
#include "lldb/ValueObject/ValueObjectVariable.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclarationName.h"
#include "clang/Basic/OperatorKinds.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/SaveAndRestore.h"

#include <algorithm>
#include <functional>

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
  void CompleteType(clang::ObjCInterfaceDecl *iface_decl) override {
    m_map.CompleteType(iface_decl);
  }

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
  if (!m_generator) {
    m_generator.emplace(*m_ast_context);
    // Let the generator resolve a type that is only forward-declared in the
    // module it was parsed from but fully defined in another module of the
    // target (TypeSystemCpp has no cross-module ASTImporter).
    m_generator->SetTarget(m_target);
  }
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
  CompilerType mapped = ClangTypeConverter(GetGenerator(), *scratch).Convert(qt);
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

void CppExpressionDeclMap::CompleteType(clang::ObjCInterfaceDecl *iface_decl) {
  if (m_ast_context)
    GetGenerator().CompleteObjCInterface(iface_decl);
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
    // Placing a generated operator decl into its (external-visible) namespace
    // makes clang reconcile the operator name there, routing an operator lookup
    // back into this function; don't re-resolve while that is in flight.
    if (m_resolving_operators)
      return false;
    if (llvm::isa<clang::TranslationUnitDecl>(dc))
      return LookupOperatorFunctions(dc, name, decls);
    // A qualified operator reference (`A::operator<`): clang looks the operator
    // name up directly in the namespace decl. Search the target for the
    // operator and surface only the overloads that actually live in this
    // namespace. The namespace decl may be one the generator created while
    // placing a namespace-scoped type, so build its routing map on demand.
    if (const auto *nsd = llvm::dyn_cast<clang::NamespaceDecl>(dc)) {
      if (m_namespace_maps.count(nsd) || EnsureGeneratorNamespaceMap(nsd))
        return LookupOperatorFunctions(dc, name, decls, nsd);
    }
    return false;
  }

  clang::IdentifierInfo *ii = name.getAsIdentifierInfo();
  if (!ii)
    return false;
  llvm::StringRef sname = ii->getName();

  // When compiling Objective-C, clang provides id/Class/SEL/Protocol
  // intrinsically. Never answer a lookup for these reserved names -- from the
  // debug info, from a namespace, or (importantly) from the synthetic
  // `$__lldb_local_vars` namespace a same-named local variable is injected
  // into (`using $__lldb_local_vars::id;`). Answering from any of those would
  // either make the reference ambiguous or let a local variable shadow the
  // builtin type, breaking `id`/`Class` as type-ids in the expression. Let
  // clang use its builtin instead. This mirrors ClangASTSource::IgnoreName's
  // unconditional (namespace-independent) suppression. (In non-ObjC code
  // these are ordinary identifiers, so only suppress them in ObjC mode.)
  if (m_ast_context->getLangOpts().ObjC &&
      (sname == "id" || sname == "Class" || sname == "SEL" ||
       sname == "Protocol"))
    return false;

  if (const auto *nsd = llvm::dyn_cast<clang::NamespaceDecl>(dc)) {
    if (nsd->getName() == "$__lldb_local_vars")
      return LookupLocalVariable(dc, ConstString(sname), decls);
    // A member lookup inside a real C++ namespace. The decl may be one we
    // created for an identifier reference (already registered) or one the
    // generator created while placing a namespace-scoped type; build the
    // routing map on demand for the latter.
    if (m_namespace_maps.count(nsd) || EnsureGeneratorNamespaceMap(nsd)) {
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

  // A lookup into a record we generated. The record keeps external visible
  // storage on so clang asks us to resolve names in it. We resolve a nested
  // type (`Record::Nested`) lazily via the generator; a nested type is not
  // emitted while completing the record (that would recurse for the
  // self-referential nested types common in libc++ containers). Because
  // turning on external visible storage routes *all* of the record's
  // name lookups through us, we must also surface its ordinary members
  // (fields, methods, static data members) that were added directly while
  // completing it -- otherwise `obj.member` / `this->member` would no longer
  // resolve.
  if (const auto *rd = llvm::dyn_cast<clang::RecordDecl>(dc)) {
    if (clang::NamedDecl *nested =
            GetGenerator().LookupNestedType(rd, sname)) {
      decls.push_back(nested);
      return true;
    }
    for (clang::Decl *d : rd->decls()) {
      auto *nd = llvm::dyn_cast<clang::NamedDecl>(d);
      if (nd && nd->getDeclName() == name)
        decls.push_back(nd);
    }
    return !decls.empty();
  }

  if (llvm::isa<clang::TranslationUnitDecl>(dc)) {
    if (sname == "$__lldb_class") {
      LookUpLldbClass(name, decls);
      return !decls.empty();
    }
    if (sname == "$__lldb_objc_class") {
      LookUpLldbObjCClass(name, decls);
      return !decls.empty();
    }
    if (sname == "$__lldb_local_vars") {
      CreateLocalVarsNamespace(dc, decls);
      return !decls.empty();
    }
    // A persistent expression variable ($0, $1, $foo, ...) referenced by the
    // expression, but not one of the internal $__lldb_* names.
    if (sname.starts_with("$") && !sname.starts_with("$__lldb")) {
      if (LookupPersistentVariable(dc, ConstString(sname), decls))
        return true;
      // Not a persistent variable: fall back to treating it as a register
      // name (e.g. `$arg1`, `$pc`, `$sp`), mirroring ClangExpressionDeclMap.
      return LookupRegister(dc, ConstString(sname), decls);
    }
    // A free function or global variable referenced by the expression
    // (e.g. `globalFuncCall()` or `g_global`).
    if (!sname.starts_with("$")) {
      // (id/Class/SEL/Protocol are already suppressed above.)
      // Skip the whole-module function search while we are synthesizing decls:
      // adding a named decl to the (external-visible) TU makes clang look that
      // name up here, but those are internal reconciliation lookups, not
      // references from the expression. Running a module-wide FindFunctions per
      // such name is what made completing large types (e.g. clang::Sema)
      // effectively never finish. Genuine references are looked up while the
      // parser runs (outside generation) and still resolve.
      if (IsGeneratingDecls())
        return false;
      // A bare name at TU scope can refer to a local variable in the current
      // frame even when local variables are not injected as members of the
      // synthetic `$__lldb_local_vars` namespace (i.e. when the
      // `target.experimental.inject-local-vars` setting is off). This mirrors
      // ClangExpressionDeclMap, which always performs a local-variable lookup
      // for unqualified names before falling back to globals.
      //
      // Skip this for a context-object evaluation
      // (SBValue::EvaluateExpression): there the expression is scoped to the
      // object, not the frame, so a frame local must never shadow a member of
      // the context object (e.g. `field` should be `this->field`, not a local
      // named `field`). ClangExpressionSourceCode likewise omits locals from
      // the wrapper when `m_ctx_obj` is set.
      if (!m_ctx_obj && LookupLocalVariable(dc, ConstString(sname), decls))
        return true;
      // Unqualified name: first honor the frame's enclosing namespace scope
      // (an expression in `A::B::f` should see names in `A::B`, then `A`), then
      // fall through to a global-scope search.
      if (LookupInFrameNamespaces(dc, ConstString(sname), decls))
        return true;
      // Inside a member function, an unqualified name can refer to a static
      // data member of the enclosing class (e.g. `s_a` for `A::s_a`). Such a
      // member is emitted like a global but is rejected by the plain global
      // search (it must not shadow a true `::s_a`); resolve it here through the
      // frame's class scope. This works for static member functions too, which
      // have no `this` pointer to reach the class through.
      if (LookupInFrameClass(dc, ConstString(sname), decls))
        return true;
      // A name at file scope can refer to a global variable or a function.
      // Prefer the variable (an object shadows a function of the same name in
      // C/C++ unqualified lookup), then fall back to functions.
      bool found = LookupGlobalVariable(dc, ConstString(sname),
                                        /*module=*/nullptr,
                                        CompilerDeclContext(), decls);
      if (!found)
        found = LookupFunctions(dc, ConstString(sname), /*module=*/nullptr,
                                CompilerDeclContext(), decls);
      // A single name can denote both an ordinary entity (a variable or
      // function) and a type/namespace of the same name -- e.g. a class `D`
      // and a global `void *D`, or a namespace and a variable. C++ ordinary
      // lookup hides the type behind the variable for a bare reference (`D`),
      // but a nested-name-specifier (`D::i`) or an elaborated type still finds
      // the class/namespace. Hand clang every candidate so it can pick the
      // right one for the syntactic context. If a variable/function was
      // already found above we still add the type/namespace here rather than
      // returning, so `D::i` resolves the class even though `void *D` shadows
      // it for `D` alone.
      found |= LookupType(dc, ConstString(sname), /*module=*/nullptr,
                          CompilerDeclContext(), decls);
      found |= LookupNamespace(dc, ConstString(sname), decls);
      // Last resort: a call to a code symbol that has no debug info (e.g. a
      // libc function like `strlen`). Only when nothing with debug info
      // matched, so a real function / variable / type / namespace is never
      // shadowed by a bare symbol (which regressed same-named debug-info
      // lookups before). The expression must cast the call to a concrete type.
      if (!found)
        found = LookupSymbolFunction(dc, ConstString(sname), decls);
      // Last resort for a data reference: a global variable that has no debug
      // info, present only as a data symbol (e.g. a hidden global in a stripped
      // translation unit). Only when nothing with debug info matched, so a real
      // variable / function / type is never shadowed by a bare symbol.
      if (!found)
        found = LookupGlobalDataSymbol(dc, ConstString(sname), decls);
      return found;
    }
  }
  return false;
}

bool CppExpressionDeclMap::LookupFunctions(
    const clang::DeclContext *dc, ConstString name, lldb::ModuleSP module,
    const CompilerDeclContext &scope,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls,
    llvm::StringSet<> *shared_seen, llvm::StringRef require_ns_name) {
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  SymbolContextList sc_list;
  ModuleFunctionSearchOptions options;
  options.include_inlines = false;
  options.include_symbols = true;
  if (scope.IsValid() && module && require_ns_name.empty()) {
    // A namespace-scoped lookup: restrict to functions declared in that
    // namespace, matching only the basename.
    module->FindFunctions(name, scope, lldb::eFunctionNameTypeBase, options,
                          sc_list);
  } else {
    target->GetImages().FindFunctions(
        name, lldb::eFunctionNameTypeFull | lldb::eFunctionNameTypeBase,
        options, sc_list);
  }

  // A target-wide search can return, alongside a global `::func()`, a file-local
  // `static func()` from another translation unit that has the same signature.
  // In C++ a file-static function in the current translation unit hides the
  // global one, so when the frame's own compile unit declares a matching
  // function, prefer it: order those candidates first so the signature dedup
  // below keeps the frame-CU-local one rather than an arbitrary same-signature
  // duplicate. (A namespace-scoped search is already unambiguous.)
  std::vector<SymbolContext> ordered(sc_list.begin(), sc_list.end());
  if (!scope.IsValid()) {
    if (StackFrame *frame = m_exe_ctx.GetFramePtr()) {
      SymbolContext frame_sc =
          frame->GetSymbolContext(lldb::eSymbolContextCompUnit);
      if (CompileUnit *frame_cu = frame_sc.comp_unit)
        std::stable_sort(
            ordered.begin(), ordered.end(),
            [frame_cu](const SymbolContext &a, const SymbolContext &b) {
              return (a.comp_unit == frame_cu) && (b.comp_unit != frame_cu);
            });
    }
  }

  clang::ASTContext &ast = *m_ast_context;
  bool added = false;
  // FindFunctions can return the same underlying function more than once (e.g.
  // matched both by base and full name, or once per module), and a name can
  // resolve to several distinct functions that share an identical signature
  // (e.g. a file-local `static` function defined in several translation units).
  // Both cases would synthesize identical-signature FunctionDecls, which clang
  // then reports as an ambiguous call. Genuine overloads differ in signature
  // and must remain distinct, so dedup by the function's type signature. A
  // caller collecting functions across several enclosing scopes passes a shared
  // set so that an outer scope's same-signature overload is dropped in favor of
  // the inner scope's (already added), while differently-typed overloads accrue.
  llvm::StringSet<> local_seen;
  llvm::StringSet<> &seen_signatures = shared_seen ? *shared_seen : local_seen;
  for (const SymbolContext &sc : ordered) {
    Function *function = sc.function;
    if (!function)
      continue;
    // When restricting to a named namespace (the lumping fallback), keep only
    // functions whose immediate enclosing namespace has that basename, e.g.
    // `A::B::func` for `require_ns_name == "B"`.
    if (!require_ns_name.empty()) {
      CompilerDeclContext fn_ctx = function->GetDeclContext();
      if (!fn_ctx || fn_ctx.GetName().GetStringRef() != require_ns_name)
        continue;
    }
    Type *func_type = function->GetType();
    if (!func_type)
      continue;
    CompilerType func_cpp_type = func_type->GetFullCompilerType();
    if (!func_cpp_type || !func_cpp_type.GetTypeSystem<TypeSystemCpp>())
      continue;

    if (!seen_signatures.insert(func_cpp_type.GetTypeName().GetStringRef())
             .second)
      continue;

    // The same target function can be reached by more than one lookup for a
    // single call (ordinary unqualified lookup plus argument-dependent lookup);
    // reuse the FunctionDecl generated the first time so the two lookups do not
    // produce two identical-signature decls (which clang reports as ambiguous).
    if (clang::FunctionDecl *cached =
            m_generated_functions.lookup(function->GetID())) {
      decls.push_back(cached);
      added = true;
      continue;
    }

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
      m_generated_functions[function->GetID()] = fd;
      decls.push_back(fd);
      added = true;
    }
  }
  return added;
}

bool CppExpressionDeclMap::LookupSymbolFunction(
    const clang::DeclContext *dc, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  // Search for a function by name including symbol-table entries; we only care
  // about matches that are symbol-only (no debug-info Function), since a
  // debug-info function was already tried and failed by the caller.
  SymbolContextList sc_list;
  ModuleFunctionSearchOptions options;
  options.include_inlines = false;
  options.include_symbols = true;
  target->GetImages().FindFunctions(
      name, lldb::eFunctionNameTypeFull | lldb::eFunctionNameTypeBase, options,
      sc_list);

  // Prefer an external symbol over a file-local one, matching the legacy
  // ClangExpressionDeclMap. Resolve re-exported symbols to their target.
  const Symbol *extern_symbol = nullptr;
  const Symbol *non_extern_symbol = nullptr;
  for (const SymbolContext &sc : sc_list) {
    // Skip anything backed by debug info: that path is handled by
    // LookupFunctions and must not be shadowed by a bare symbol here.
    if (sc.function)
      continue;
    Symbol *symbol = sc.symbol;
    if (!symbol)
      continue;
    if (symbol->GetType() == eSymbolTypeReExported) {
      symbol = symbol->ResolveReExportedSymbol(*target);
      if (!symbol)
        continue;
    }
    // Only code (or indirect/resolver) symbols are callable.
    switch (symbol->GetType()) {
    case eSymbolTypeCode:
    case eSymbolTypeResolver:
    case eSymbolTypeReExported:
      break;
    default:
      continue;
    }
    if (symbol->IsExternal())
      extern_symbol = symbol;
    else
      non_extern_symbol = symbol;
  }

  const Symbol *symbol = extern_symbol ? extern_symbol : non_extern_symbol;
  if (!symbol)
    return false;

  clang::FunctionDecl *fd =
      GetGenerator().GenerateGenericFunction(name.GetStringRef());
  if (!fd)
    return false;
  if (dc && dc != m_ast_context->getTranslationUnitDecl()) {
    fd->setDeclContext(const_cast<clang::DeclContext *>(dc));
    const_cast<clang::DeclContext *>(dc)->addDecl(fd);
  }

  // Bind the decl to the symbol so the materializer resolves the callee to the
  // symbol's load address (the generic decl carries no asm label).
  auto *entity = new ClangExpressionVariable(
      m_exe_ctx.GetBestExecutionContextScope(), m_byte_order, m_addr_byte_size);
  m_found_entities.AddNewlyConstructedVariable(entity);
  entity->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *pv = entity->GetParserVars(GetParserID());
  pv->m_named_decl = fd;
  pv->m_llvm_value = nullptr;
  pv->m_lldb_sym = symbol;

  decls.push_back(fd);
  return true;
}

bool CppExpressionDeclMap::LookupGlobalDataSymbol(
    const clang::DeclContext *dc, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  // Build the symbol context used to pick the best data symbol. Seeding it with
  // the frame's module makes a symbol in the currently-executing module win over
  // an identically-named one in another module (the "conflicting symbol" case),
  // matching the legacy ClangExpressionDeclMap, which searches from the
  // expression's own symbol context.
  SymbolContext sym_ctx;
  sym_ctx.target_sp = target->shared_from_this();
  if (StackFrame *frame = m_exe_ctx.GetFramePtr()) {
    SymbolContext frame_sc = frame->GetSymbolContext(lldb::eSymbolContextModule);
    sym_ctx.module_sp = frame_sc.module_sp;
  }

  Status error;
  const Symbol *symbol = sym_ctx.FindBestGlobalDataSymbol(name, error);
  // FindBestGlobalDataSymbol reports an ambiguity (e.g. "Multiple internal
  // symbols found") through `error`; surface it as a parser diagnostic so the
  // expression fails with that message rather than an "undeclared identifier".
  if (!error.Success() && m_diagnostics)
    m_diagnostics->AddDiagnostic(error.AsCString(), lldb::eSeverityError,
                                 DiagnosticOrigin::eDiagnosticOriginLLDB);
  if (!symbol)
    return false;

  // A bare data symbol has no debug type; model it as `void *&` (a reference to
  // pointer-sized storage) so the materializer can bind the decl to the
  // symbol's load address, mirroring the legacy AddOneGenericVariable path.
  clang::QualType void_ptr_ref = m_ast_context->getLValueReferenceType(
      m_ast_context->getPointerType(m_ast_context->VoidTy));

  auto *vd = clang::VarDecl::Create(
      *m_ast_context, const_cast<clang::DeclContext *>(dc),
      clang::SourceLocation(), clang::SourceLocation(),
      &m_ast_context->Idents.get(name.GetStringRef()), void_ptr_ref, nullptr,
      clang::SC_Static);
  decls.push_back(vd);

  auto *entity = new ClangExpressionVariable(
      m_exe_ctx.GetBestExecutionContextScope(), m_byte_order, m_addr_byte_size);
  m_found_entities.AddNewlyConstructedVariable(entity);
  entity->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *pv = entity->GetParserVars(GetParserID());
  pv->m_named_decl = vd;
  pv->m_llvm_value = nullptr;
  pv->m_lldb_sym = symbol;
  // The decl is a reference type, so its value is the storage's address.
  entity->m_flags |= ClangExpressionVariable::EVTypeIsReference;
  return true;
}

bool CppExpressionDeclMap::LookupOperatorFunctions(
    const clang::DeclContext *dc, clang::DeclarationName name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls,
    const clang::NamespaceDecl *scope_ns) {
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  // Guard against the reentrant operator lookup clang runs while we add a
  // generated operator decl into its external-visible namespace below.
  llvm::SaveAndRestore<bool> resolving(m_resolving_operators, true);

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

    // A qualified lookup (`A::operator<`) must only surface operators that
    // actually live in the requested namespace; skip any operator declared
    // elsewhere (including global scope).
    clang::DeclContext *op_dc = GetOperatorDeclContext(function);
    if (scope_ns && op_dc != static_cast<const clang::DeclContext *>(scope_ns))
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
    clang::FunctionDecl *fd =
        GetGenerator().GenerateFunction(name, func_cpp_type, label);
    if (!fd)
      continue;

    // A namespace-scoped free operator (e.g. `A::operator<`) is found during
    // operator-syntax overload resolution (`b < b` for an `A::B`) only through
    // argument-dependent lookup, which searches the operands' associated
    // namespaces -- here namespace `A`. Unqualified operator lookup alone (which
    // is what a translation-unit-scoped decl would satisfy) does not find it, so
    // place the generated decl in the clang NamespaceDecl matching the
    // operator's own namespace. This both enables ADL and gives the decl the
    // correct qualified/mangled name. A global operator stays at TU scope.
    if (op_dc && op_dc != m_ast_context->getTranslationUnitDecl()) {
      m_ast_context->getTranslationUnitDecl()->removeDecl(fd);
      fd->setDeclContext(op_dc);
      fd->setLexicalDeclContext(op_dc);
      op_dc->addDecl(fd);
    }
    decls.push_back(fd);
    added = true;
  }
  return added;
}

clang::DeclContext *
CppExpressionDeclMap::GetOperatorDeclContext(Function *function) {
  if (!function)
    return nullptr;
  CompilerDeclContext ctx = function->GetDeclContext();
  if (!ctx || !llvm::isa_and_nonnull<TypeSystemCpp>(ctx.GetTypeSystem()))
    return nullptr;
  // A free operator declared directly at global scope has no enclosing
  // TypeSystemCpp namespace; keep it at translation-unit scope.
  auto *cpp_ns = static_cast<const cpp_typesystem::Namespace *>(
      ctx.GetOpaqueDeclContext());
  if (!cpp_ns)
    return nullptr;
  return GetGenerator().GetDeclContextForNamespace(cpp_ns);
}

void CppExpressionDeclMap::LookUpLldbClass(
    clang::DeclarationName name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  CompilerType class_cpp_type;

  // A context-object evaluation (SBValue::EvaluateExpression) supplies the
  // enclosing object directly via `m_ctx_obj`; the injected wrapper method
  // gets an implicit `this` of that object's type so unqualified names resolve
  // as members of it (e.g. evaluating `field` against a struct value finds the
  // struct's member, not a same-named local). Mirrors
  // ClangExpressionDeclMap::LookUpLldbClass's `m_ctx_obj` branch.
  if (m_ctx_obj) {
    // The wrapper's implicit `this` is bound to the context object's live
    // address at materialization time; a computed rvalue (e.g. the result of
    // `GetCppStruct()`) has no address, so evaluating a member expression
    // against it must fail rather than silently succeed. Refuse to provide
    // `$__lldb_class` in that case so the wrapper fails to compile, matching
    // ClangExpressionDeclMap (which returns early when `AddressOf` fails).
    Status status;
    lldb::ValueObjectSP ctx_obj_ptr = m_ctx_obj->AddressOf(status);
    if (!ctx_obj_ptr || status.Fail())
      return;
    class_cpp_type = m_ctx_obj->GetCompilerType();
  } else {
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
    class_cpp_type = this_type->GetForwardCompilerType().GetPointeeType();

    // Inside a lambda that captured `this`, the frame's `this` is the (unnamed)
    // lambda closure object, whose captured `this` is a member named `this`
    // (DWARF) / `__this` (CodeView). Use the captured object's class as
    // `$__lldb_class` so unqualified member lookups resolve against the
    // enclosing class, mirroring ClangExpressionDeclMap::LookUpLldbClass and the
    // captured-`this` handling in ClangExpressionSourceCode.
    if (ValueObjectSP this_valobj =
            ValueObjectVariable::Create(frame, this_var)) {
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

  clang::ASTContext &ast = *m_ast_context;

  // Only a class/struct/union has members and can host the injected
  // `$__lldb_expr` method. For a non-record context object (e.g. a scalar,
  // pointer or array supplied to SBValue::EvaluateExpression), still emit the
  // `$__lldb_class` typedef but add no method: the wrapper is then
  // `<non-record>::$__lldb_expr`, which fails to compile, matching
  // TypeSystemClang (a bare expression like `1` against a scalar object is
  // expected to error rather than silently evaluate).
  if (auto *record = class_qt->getAsCXXRecordDecl()) {
    // Make sure members are available for unqualified lookup.
    GetGenerator().CompleteRecord(record);

    // Declare `void $__lldb_expr(void *)` in the class so the wrapper's
    // out-of-line `$__lldb_class::$__lldb_expr` definition has a matching
    // declaration (and thus an implicit `this`). Mirror the cv-qualifiers of
    // the frame's actual method so the implicit `this` is `const`/`volatile`
    // too.
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
    clang::QualType method_qt =
        ast.getFunctionType(ast.VoidTy, param_types, epi);
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
  }

  // Provide the `$__lldb_class` typedef the wrapper names.
  clang::TypeSourceInfo *ti = ast.getTrivialTypeSourceInfo(class_qt);
  auto *td = clang::TypedefDecl::Create(
      ast, ast.getTranslationUnitDecl(), clang::SourceLocation(),
      clang::SourceLocation(), name.getAsIdentifierInfo(), ti);
  decls.push_back(td);
}

void CppExpressionDeclMap::LookUpLldbObjCClass(
    clang::DeclarationName name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  CompilerType class_cpp_type;

  // A context-object evaluation (SBValue::EvaluateExpression) supplies the
  // enclosing object directly via `m_ctx_obj`; use its interface so the
  // wrapper's `$__lldb_objc_class` context gives the injected method an
  // implicit `self` of the right type. Mirrors LookUpLldbClass's `m_ctx_obj`
  // branch.
  if (m_ctx_obj) {
    // A computed rvalue has no live address to bind `self` to; refuse so the
    // wrapper fails to compile rather than silently evaluating.
    Status status;
    lldb::ValueObjectSP ctx_obj_ptr = m_ctx_obj->AddressOf(status);
    if (!ctx_obj_ptr || status.Fail())
      return;
    // The context object is the ObjC object itself; its type is the interface
    // (or a pointer to it). Take the pointee when it is a pointer.
    CompilerType ctx_type = m_ctx_obj->GetCompilerType();
    class_cpp_type =
        ctx_type.IsPointerType(nullptr) ? ctx_type.GetPointeeType() : ctx_type;
  } else {
    StackFrame *frame = m_exe_ctx.GetFramePtr();
    if (!frame)
      return;

    // Determine whether the enclosing method is a class (`+`) method from its
    // mangled `+[Class sel]` / `-[Class sel]` name (mirrors the detection in
    // ClangUserExpression::ScanContext, which set up `m_in_static_method` the
    // same way). For a class method, `self`'s static type is `Class` (a
    // metaclass pointer, not a pointer to the interface), so it can't be used
    // to recover the interface the way the instance-method branch below does;
    // instead resolve the interface by name via LookupType, which already
    // handles both the debug-info and ObjC-runtime (no debug info) cases.
    SymbolContext sym_ctx =
        frame->GetSymbolContext(lldb::eSymbolContextFunction);
    llvm::StringRef fname = sym_ctx.function
                                ? sym_ctx.function->GetName().GetStringRef()
                                : llvm::StringRef();
    std::optional<ObjCLanguage::ObjCMethodName> method_name =
        ObjCLanguage::ObjCMethodName::Create(fname, /*strict=*/true);
    if (method_name && method_name->IsClassMethod()) {
      llvm::StringRef class_name = method_name->GetClassName();
      if (class_name.empty())
        return;
      llvm::SmallVector<clang::NamedDecl *, 1> type_decls;
      if (!LookupType(m_ast_context->getTranslationUnitDecl(),
                      ConstString(class_name), /*module=*/nullptr,
                      CompilerDeclContext(), type_decls))
        return;
      for (clang::NamedDecl *type_decl : type_decls) {
        if (auto *iface_decl =
                llvm::dyn_cast<clang::ObjCInterfaceDecl>(type_decl)) {
          decls.push_back(iface_decl);
          return;
        }
      }
      return;
    }

    // The enclosing Objective-C instance method's object type is the pointee
    // of the frame's implicit `self` parameter. Mirrors
    // ClangExpressionDeclMap::LookUpLldbObjCClass, but instead of consulting a
    // clang ObjCMethodDecl we go straight through the `self` variable (whose
    // pointee is the method's ObjCInterfaceType). Returning that interface for
    // the wrapper's `$__lldb_objc_class` gives the injected method an implicit
    // `self` of the right type, so unqualified ivar names resolve as ObjC ivar
    // accesses on `self` (just as C++ members resolve through `this`).
    VariableListSP vars = frame->GetInScopeVariableList(true);
    VariableSP self_var =
        vars ? vars->FindVariable(ConstString("self")) : VariableSP();
    if (!self_var)
      return;
    Type *self_type = self_var->GetType();
    if (!self_type)
      return;

    CompilerType self_cpp_type = self_type->GetForwardCompilerType();
    // `self` is a pointer to the interface (an ObjC object pointer); the class
    // is its pointee.
    class_cpp_type = self_cpp_type.GetPointeeType();
  }
  if (!class_cpp_type || !class_cpp_type.GetTypeSystem<TypeSystemCpp>())
    return;

  clang::QualType class_qt = GetGenerator().Generate(class_cpp_type);
  if (class_qt.isNull())
    return;

  // The generated type for an ObjCInterfaceType is an ObjCInterfaceType (an
  // ObjCObjectType); pull out its ObjCInterfaceDecl and make its ivars
  // available for unqualified lookup through `self`.
  clang::ObjCInterfaceDecl *iface_decl = nullptr;
  if (const auto *obj_type = class_qt->getAs<clang::ObjCObjectType>())
    iface_decl = obj_type->getInterface();
  if (!iface_decl)
    return;
  GetGenerator().CompleteObjCInterface(iface_decl);

  // Hand clang the real interface decl as the answer for `$__lldb_objc_class`.
  // The wrapper declares `@interface $__lldb_objc_class (...)`; clang tolerates
  // the returned interface carrying its true name (e.g. `A`), mirroring the
  // legacy NameSearchContext::AddTypeDecl ObjC path.
  decls.push_back(iface_decl);
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
  if (!type_sp) {
    // No debug info at all defines this name (e.g. an Objective-C
    // @implementation compiled with -g0, so it has no DWARF type in any
    // module for FindTypes to find -- see TestObjCiVarIMP). Ask the ObjC
    // runtime by class name as a last resort, building the interface's ivar
    // list from its ClassDescriptor (mirrors
    // TypeSystemCpp::GetRuntimeCompletedObjCType, which does the same for a
    // value whose *static* type is already known but incomplete; here there
    // is no static type at all, only the bare name).
    //
    // Cheaply gate on an `OBJC_CLASS_$_<name>` symbol existing at all before
    // asking the runtime: ObjCLanguageRuntime::GetClassDescriptorFromClassName
    // (called by CreateRuntimeObjCInterface) triggers a full class-list scan
    // the first time it runs, which is itself a nested JIT'd expression
    // evaluation -- doing that for every plain, non-ObjC identifier that
    // merely lacks a debug-info type (e.g. a global C++ function looked up
    // before its FunctionDecl is found) corrupted unrelated, concurrent
    // expression-parser state (TestNamespaceLookup's `foo()` started
    // resolving to the wrong candidate once this ran for it).
    if (!scope.IsValid()) {
      ConstString class_symbol(("OBJC_CLASS_$_" + name.GetStringRef()).str());
      SymbolContextList sc_list;
      target->GetImages().FindSymbolsWithNameAndType(
          class_symbol, lldb::eSymbolTypeObjCClass, sc_list);
      if (!sc_list.IsEmpty()) {
        if (lldb::ProcessSP process_sp = target->GetProcessSP()) {
          if (ObjCLanguageRuntime *runtime =
                  ObjCLanguageRuntime::Get(*process_sp)) {
            if (TypeSystemCpp *scratch = GetScratchCpp(target)) {
              CompilerType runtime_ct = scratch->CreateRuntimeObjCInterface(
                  name, *process_sp, *runtime);
              if (runtime_ct) {
                clang::QualType qt = GetGenerator().Generate(runtime_ct);
                if (!qt.isNull()) {
                  if (const auto *obj_type =
                          qt->getAs<clang::ObjCObjectType>()) {
                    if (clang::ObjCInterfaceDecl *iface_decl =
                            obj_type->getInterface()) {
                      GetGenerator().CompleteObjCInterface(iface_decl);
                      decls.push_back(iface_decl);
                      return true;
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    return false;
  }
  // Skip types that live in a JIT'd expression / utility-function module: those
  // are LLDB-internal artifacts (a previous expression's own structs, emitted
  // with debug info), not real program types. Surfacing one makes a later
  // expression that defines a same-named type -- e.g. the ObjC runtime's
  // `struct ClassInfo` in its class-scan utility functions -- fail with a
  // spurious "redefinition" against the earlier utility function's copy.
  if (lldb::ModuleSP module = type_sp->GetModule())
    if (ObjectFile *obj = module->GetObjectFile())
      if (obj->GetType() == ObjectFile::eTypeJIT)
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

  // A typedef must surface its own TypedefDecl (whose name is the alias, e.g.
  // `NamespaceTypedef`), not the decl of the type it aliases. Check this before
  // getAsTagDecl(), which sees through the typedef sugar to the underlying tag
  // (e.g. `S<float>`); returning that tag under the alias name makes clang's
  // name reconciliation assert on a declaration-name mismatch.
  if (const auto *tdt = qt->getAs<clang::TypedefType>()) {
    decls.push_back(tdt->getDecl());
    return true;
  }
  if (clang::TagDecl *tag = qt->getAsTagDecl()) {
    // Make sure the members/enumerators are available (e.g. for sizeof or
    // offsetof, which need a complete type).
    if (auto *record = llvm::dyn_cast<clang::RecordDecl>(tag))
      GetGenerator().CompleteRecord(record);
    decls.push_back(tag);
    return true;
  }
  // An Objective-C class (`@interface Foo`) is an ObjCObjectType, not a
  // TagType. Surface its ObjCInterfaceDecl so a bare `Foo` names the class --
  // needed for a class message send (`[Foo sel:...]`) and for casting. Complete
  // it so its methods/ivars are available.
  if (const auto *obj_type = qt->getAs<clang::ObjCObjectType>()) {
    if (clang::ObjCInterfaceDecl *iface_decl = obj_type->getInterface()) {
      GetGenerator().CompleteObjCInterface(iface_decl);
      decls.push_back(iface_decl);
      return true;
    }
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

  return MaterializeNamespaceMap(parent_dc, name, std::move(map), decls);
}

bool CppExpressionDeclMap::MaterializeNamespaceMap(
    const clang::DeclContext *parent_dc, ConstString name, NamespaceMap map,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  if (map.empty() || !m_ast_context)
    return false;

  // The representative cpp namespace (first module's decl context) this clang
  // NamespaceDecl stands for. Used both to reuse a decl the generator already
  // made and to register the mapping for future type generation.
  auto *cpp_ns = static_cast<const cpp_typesystem::Namespace *>(
      map.front().second.GetOpaqueDeclContext());

  // The generator may already have materialized a clang::NamespaceDecl for this
  // cpp namespace (e.g. while generating a namespace-scoped type such as
  // `A::B`). Reuse it rather than create a second NamespaceDecl for the same
  // name: two decls become a redeclaration chain whose primary context is the
  // generator's (which lacks external visible storage), so a later lookup in
  // the namespace -- notably a qualified operator like `A::operator<` -- would
  // never call back into us. Turn on external visible storage on the existing
  // decl so member lookups route here.
  clang::ASTContext &ast = *m_ast_context;
  if (clang::NamespaceDecl *nsd = GetGenerator().GetRegisteredNamespace(cpp_ns)) {
    clang::Decl::castToDeclContext(nsd)->setHasExternalVisibleStorage(true);
    m_namespace_maps.try_emplace(nsd, std::move(map));
    decls.push_back(nsd);
    return true;
  }

  // Create the parser-side namespace decl and cache its map. The decl is given
  // external visible storage so clang calls back into us for member lookups
  // (LookupInNamespace). Register it -- both with the generator and in the
  // routing map -- *before* adding it to its (external-visible) parent: doing
  // so makes clang reconcile the namespace's name, which routes a lookup for it
  // straight back here; having it registered first lets that reentrant lookup
  // reuse this decl (above) instead of recursing without end.
  auto *nsd = clang::NamespaceDecl::Create(
      ast, const_cast<clang::DeclContext *>(parent_dc), /*Inline=*/false,
      clang::SourceLocation(), clang::SourceLocation(),
      &ast.Idents.get(name.GetStringRef()), /*PrevDecl=*/nullptr,
      /*Nested=*/false);
  clang::Decl::castToDeclContext(nsd)->setHasExternalVisibleStorage(true);
  GetGenerator().RegisterNamespace(cpp_ns, nsd);
  m_namespace_maps.insert({nsd, std::move(map)});
  const_cast<clang::DeclContext *>(parent_dc)->addDecl(nsd);

  decls.push_back(nsd);
  return true;
}

bool CppExpressionDeclMap::EnsureGeneratorNamespaceMap(
    const clang::NamespaceDecl *nsd) {
  if (m_namespace_maps.count(nsd))
    return true;

  // Only a namespace decl the generator materialized itself (while placing a
  // namespace-scoped type) can be recovered this way; a decl the identifier
  // path created is already registered.
  const cpp_typesystem::Namespace *cpp_ns =
      GetGenerator().GetNamespaceForDecl(nsd);
  if (!cpp_ns)
    return false;

  Target *target = m_exe_ctx.GetTargetPtr();
  if (!target)
    return false;

  // Resolve, per module, the CompilerDeclContext whose cpp namespace is exactly
  // `cpp_ns`. FindNamespace matches by name within a parent scope, so walk the
  // parent chain (outermost-first) to build that scope, and confirm identity by
  // comparing the opaque cpp namespace pointer.
  std::function<CompilerDeclContext(SymbolFile *,
                                    const cpp_typesystem::Namespace *)>
      resolve = [&](SymbolFile *sf,
                    const cpp_typesystem::Namespace *ns) -> CompilerDeclContext {
    if (!ns)
      return CompilerDeclContext();
    CompilerDeclContext parent = resolve(sf, ns->GetParent());
    // A parent that exists but couldn't be resolved means this chain isn't in
    // this module.
    if (ns->GetParent() && !parent.IsValid())
      return CompilerDeclContext();
    ConstString name(ns->GetName().GetName());
    CompilerDeclContext found = sf->FindNamespace(name, parent);
    if (found.IsValid() &&
        llvm::isa_and_nonnull<TypeSystemCpp>(found.GetTypeSystem()) &&
        found.GetOpaqueDeclContext() == ns)
      return found;
    return CompilerDeclContext();
  };

  NamespaceMap map;
  for (const lldb::ModuleSP &module_sp : target->GetImages().Modules()) {
    if (!module_sp)
      continue;
    SymbolFile *sf = module_sp->GetSymbolFile();
    if (!sf)
      continue;
    CompilerDeclContext found = resolve(sf, cpp_ns);
    if (found.IsValid())
      map.emplace_back(module_sp, found);
  }

  if (map.empty())
    return false;

  m_namespace_maps.insert({nsd, std::move(map)});
  return true;
}

bool CppExpressionDeclMap::LookupInNamespace(
    const clang::NamespaceDecl *nsd, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  auto it = m_namespace_maps.find(nsd);
  if (it == m_namespace_maps.end())
    return false;  const NamespaceMap &map = it->second;
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

  // TypeSystemClang lumps every namespace of the same name into one clang
  // NamespaceDecl, so a qualified `B::func()` can bind to `A::B::func` even
  // when the `B` clang resolved (e.g. a global `::B`) declares no `func`. The
  // per-module NamespaceMap above only carries the one resolved `B`, so on a
  // miss fall back to a target-wide function search restricted to any namespace
  // named like this one.
  if (!added)
    added = LookupFunctions(dc, name, /*module=*/nullptr, CompilerDeclContext(),
                            decls, /*shared_seen=*/nullptr,
                            /*require_ns_name=*/nsd->getName());
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

  // First honor any `using` declaration (e.g. `using Single::single;`) lexically
  // in scope at the current PC: it names a specific entity from another
  // namespace, so an unqualified reference to that name resolves there. A
  // using-declaration acts like a declaration in its scope and so takes
  // precedence over using-directives.
  Block *pc_block = sc.block ? sc.block : function_block;
  for (const auto &decl : ts->GetUsingDeclarations(*pc_block)) {
    if (decl.first != name || !decl.second.IsValid())
      continue;
    if (LookupGlobalVariable(dc, name, module, decl.second, decls))
      return true;
    if (LookupFunctions(dc, name, module, decl.second, decls))
      return true;
    if (LookupType(dc, name, module, decl.second, decls))
      return true;
  }

  // Then honor the `using namespace` directives lexically in scope at the
  // current PC: a `using namespace ns2;` makes an unqualified `value` resolve
  // to `ns2::value`. Search the innermost block containing the PC (falling back
  // to the function block).
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
  // Functions are collected across every enclosing scope (not just the first),
  // so an unqualified call can bind to an overload declared in an outer scope
  // even when an inner scope declares the same name -- lldb intentionally
  // diverges from C++ name hiding here and merges the overload sets, deduping
  // only identical signatures (the inner scope's declaration wins). A variable
  // or type, by contrast, is name-hidden: the innermost scope that declares it
  // wins and stops the walk.
  llvm::StringSet<> fn_seen;
  bool found_function = false;
  for (auto *ns = static_cast<const cpp_typesystem::Namespace *>(
           frame_ctx.GetOpaqueDeclContext());
       ns; ns = ns->GetParent()) {
    CompilerDeclContext scope(ts, const_cast<cpp_typesystem::Namespace *>(ns));
    if (!found_function &&
        LookupGlobalVariable(dc, name, module, scope, decls))
      return true;
    if (LookupFunctions(dc, name, module, scope, decls, &fn_seen))
      found_function = true;
    if (!found_function && LookupType(dc, name, module, scope, decls))
      return true;
  }
  return found_function;
}

bool CppExpressionDeclMap::LookupInFrameClass(
    const clang::DeclContext *dc, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Log *log = GetLog(LLDBLog::Expressions);
  StackFrame *frame = m_exe_ctx.GetFramePtr();
  Target *target = m_exe_ctx.GetTargetPtr();
  if (!frame || !target)
    return false;

  SymbolContext sc = frame->GetSymbolContext(lldb::eSymbolContextFunction |
                                             lldb::eSymbolContextBlock);
  Block *function_block = sc.GetFunctionBlock();
  if (!function_block || !sc.module_sp)
    return false;

  auto ts_or_err =
      sc.module_sp->GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (!ts_or_err) {
    llvm::consumeError(ts_or_err.takeError());
    return false;
  }
  auto *ts = llvm::dyn_cast_or_null<TypeSystemCpp>(ts_or_err->get());
  if (!ts)
    return false;

  // The class the frame function is a member of (if any). Derived from the
  // function itself, so this also covers static member functions (no `this`).
  CompilerType class_type = ts->GetOwningClassForFunction(*function_block);
  if (!class_type)
    return false;

  // A static data member of `Class` is emitted as a global whose qualified name
  // is `Class::member` (e.g. `A<int>::s_a`). Build that spelling to match it.
  ConstString class_name = class_type.GetTypeName();
  if (!class_name)
    return false;
  std::string qualified = class_name.GetStringRef().str();
  qualified += "::";
  qualified += name.GetStringRef();
  ConstString qualified_name(qualified);

  VariableList vars;
  target->GetImages().FindGlobalVariables(name, -1, vars);
  for (size_t i = 0, e = vars.GetSize(); i != e; ++i) {
    VariableSP var = vars.GetVariableAtIndex(i);
    // Only a static data member of exactly this class (matched by its
    // fully-qualified name) resolves for an unqualified reference here.
    if (!var || !var->IsStaticMember() || var->GetName() != qualified_name)
      continue;
    if (var->GetUnqualifiedName() != name)
      continue;

    Type *var_type = var->GetType();
    if (!var_type)
      continue;
    CompilerType var_cpp_type = var_type->GetFullCompilerType();
    if (!var_cpp_type || !var_cpp_type.GetTypeSystem<TypeSystemCpp>())
      continue;

    clang::QualType qt = GetGenerator().Generate(var_cpp_type);
    if (qt.isNull()) {
      LLDB_LOG(log,
               "CppEDM: couldn't generate a parser type for static member {0}",
               qualified_name);
      continue;
    }

    // Represented by a reference to its storage (unless already a reference),
    // like the global-variable path, so the materializer binds it to the
    // member's load address.
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

bool CppExpressionDeclMap::LookupRegister(
    const clang::DeclContext *dc, ConstString name,
    llvm::SmallVectorImpl<clang::NamedDecl *> &decls) {
  Log *log = GetLog(LLDBLog::Expressions);

  RegisterContext *reg_ctx = m_exe_ctx.GetRegisterContext();
  if (!reg_ctx)
    return false;

  assert(name.GetStringRef().starts_with("$"));
  llvm::StringRef reg_name = name.GetStringRef().substr(1);

  const RegisterInfo *reg_info = reg_ctx->GetRegisterInfoByName(reg_name);
  if (!reg_info)
    return false;

  TypeSystemCpp *scratch = GetScratchCpp(m_exe_ctx.GetTargetPtr());
  if (!scratch)
    return false;

  CompilerType cpp_type = scratch->GetBuiltinTypeForEncodingAndBitSize(
      reg_info->encoding, reg_info->byte_size * 8);
  if (!cpp_type) {
    LLDB_LOG(log, "CppEDM: couldn't get a builtin type for register {0}",
             reg_info->name);
    return false;
  }

  clang::QualType qt = GetGenerator().Generate(cpp_type);
  if (qt.isNull()) {
    LLDB_LOG(log, "CppEDM: couldn't generate a parser type for register {0}",
             reg_info->name);
    return false;
  }

  auto *vd = clang::VarDecl::Create(
      *m_ast_context, const_cast<clang::DeclContext *>(dc),
      clang::SourceLocation(), clang::SourceLocation(),
      &m_ast_context->Idents.get(name.GetStringRef()), qt, nullptr,
      clang::SC_Static);
  decls.push_back(vd);

  auto *entity = new ClangExpressionVariable(
      m_exe_ctx.GetBestExecutionContextScope(), m_byte_order,
      m_addr_byte_size);
  m_found_entities.AddNewlyConstructedVariable(entity);
  entity->SetName(name.GetStringRef());
  entity->SetRegisterInfo(reg_info);
  entity->EnableParserVars(GetParserID());
  ClangExpressionVariable::ParserVars *parser_vars =
      entity->GetParserVars(GetParserID());
  parser_vars->m_named_decl = vd;
  parser_vars->m_llvm_value = nullptr;
  parser_vars->m_lldb_value.Clear();
  entity->m_flags |= ClangExpressionVariable::EVBareRegister;

  LLDB_LOG(log, "CppEDM: Added register {0}", reg_info->name);
  return true;
}

bool CppExpressionDeclMap::AddPersistentVariable(const clang::NamedDecl *decl,
                                                 ConstString name,
                                                 TypeFromParser type,
                                                 bool is_result,
                                                 bool is_lvalue) {
  Log *log = GetLog(LLDBLog::Expressions);

  // `type` was produced by WrapType, so it is already a TypeSystemCpp type
  // living in the scratch TypeSystemCpp.
  if (!type) {
    LLDB_LOG(log, "CppEDM: persistent variable type couldn't be mapped to a "
                  "TypeSystemCpp type");
    return false;
  }

  TypeFromUser user_type(type);

  // The expression result ($0, $1, ...) is materialized as a result variable in
  // the current expression's struct; the materializer wires up its storage.
  if (is_result && m_materializer) {
    Status err;
    uint32_t offset = m_materializer->AddResultVariable(
        user_type, is_lvalue, m_keep_result_in_memory, m_result_delegate, err);
    if (!err.Success()) {
      LLDB_LOG(log, "CppEDM: couldn't add result variable: {0}",
               err.AsCString());
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

  // An explicitly-declared persistent variable ($foo = ...) lives in the
  // persistent expression state so it survives across expression evaluations.
  if (!m_persistent_vars)
    return false;

  // Reject a redefinition rather than silently shadowing the existing one.
  if (m_persistent_vars->GetVariable(name)) {
    if (m_diagnostics) {
      std::string msg =
          llvm::formatv("redefinition of persistent variable '{0}'", name)
              .str();
      m_diagnostics->AddDiagnostic(msg, lldb::eSeverityError,
                                   DiagnosticOrigin::eDiagnosticOriginLLDB);
    }
    return false;
  }

  auto *var = llvm::cast<ClangExpressionVariable>(
      m_persistent_vars
          ->CreatePersistentVariable(m_exe_ctx.GetBestExecutionContextScope(),
                                     name, user_type, m_byte_order,
                                     m_addr_byte_size)
          .get());
  if (!var)
    return false;

  var->m_frozen_sp->SetHasCompleteType();

  if (is_result)
    var->m_flags |= ClangExpressionVariable::EVNeedsFreezeDry;
  else
    // Explicitly-declared persistent variables should persist.
    var->m_flags |= ClangExpressionVariable::EVKeepInTarget;

  if (is_lvalue) {
    var->m_flags |= ClangExpressionVariable::EVIsProgramReference;
  } else {
    var->m_flags |= ClangExpressionVariable::EVIsLLDBAllocated;
    var->m_flags |= ClangExpressionVariable::EVNeedsAllocation;
  }

  if (m_keep_result_in_memory)
    var->m_flags |= ClangExpressionVariable::EVKeepInTarget;

  LLDB_LOG(log, "CppEDM: created persistent variable {0} with flags {1:x}", name,
           var->m_flags);

  var->EnableParserVars(GetParserID());
  var->GetParserVars(GetParserID())->m_named_decl = decl;
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
    } else if (parser_vars->m_lldb_sym)
      member_offset = m_materializer->AddSymbol(*parser_vars->m_lldb_sym, err);
    else if (const RegisterInfo *reg_info = var->GetRegisterInfo())
      member_offset = m_materializer->AddRegister(*reg_info, err);
    else if (parser_vars->m_lldb_var)
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
  // No symbol table entry (e.g. a stripped binary debugged via a separate
  // dSYM: the loaded image's own symtab has no `OBJC_IVAR_$_Class.ivar`
  // entry, even though the dSYM's debug info still describes the ivar) --
  // ask the ObjC runtime, which can resolve some symbols (indirect ivar
  // offset globals in particular) directly from the loaded class's runtime
  // metadata instead of a static symbol table. Mirrors
  // ClangExpressionDeclMap::GetSymbolAddress's fallback.
  if (lldb::ProcessSP process_sp = m_exe_ctx.GetProcessSP()) {
    if (ObjCLanguageRuntime *runtime = ObjCLanguageRuntime::Get(*process_sp)) {
      lldb::addr_t runtime_addr = runtime->LookupRuntimeSymbol(name);
      if (runtime_addr != LLDB_INVALID_ADDRESS)
        return runtime_addr;
    }
  }
  return LLDB_INVALID_ADDRESS;
}
