//===-- ClikeExpressionDeclMap.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLIKEEXPRESSIONDECLMAP_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLIKEEXPRESSIONDECLMAP_H

#include "ClangASTGenerator.h"
#include "ClangExpressionVariable.h"
#include "ExpressionDeclMap.h"

#include "lldb/Core/Value.h"
#include "lldb/Expression/Materializer.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Target/ExecutionContext.h"
#include "lldb/lldb-public.h"

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringSet.h"

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace clang {
class ASTContext;
class DeclContext;
class DeclarationName;
class FunctionDecl;
class NamedDecl;
class NamespaceDecl;
class ObjCInterfaceDecl;
class RecordDecl;
class TagDecl;
} // namespace clang

namespace lldb_private {

class PersistentExpressionState;

/// Resolves external entities for a Clang-parsed expression out of the
/// module-level TypeSystemClike (which has no Clang AST), and lays out the
/// materialization struct / reports results.
///
/// This is the TypeSystemClike counterpart of ClangExpressionDeclMap. It does not
/// inherit from it and never references TypeSystemClang: it acts as a
/// clang::ExternalASTSource (through a proxy), synthesizes the Clang types the
/// parser needs via ClangASTGenerator (writing into the parser's raw
/// clang::ASTContext), and maps the expression's result type back onto a
/// TypeSystemClike type stored in the scratch TypeSystemClike.
class ClikeExpressionDeclMap : public ExpressionDeclMap {
public:
  ClikeExpressionDeclMap(bool keep_result_in_memory,
                       Materializer::PersistentVariableDelegate *result_delegate,
                       const lldb::TargetSP &target, ValueObject *ctx_obj,
                       bool ignore_context_qualifiers);
  ~ClikeExpressionDeclMap() override;

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
  bool IsClikeDeclMap() const override { return true; }

  /// Give the map the parser's clang::ASTContext (called from
  /// ClangExpressionParser::ParseInternal).
  void InstallASTContext(clang::ASTContext &ast);

  // Called by the ExternalASTSource proxy (see CreateProxy).
  bool FindExternalVisibleDecls(const clang::DeclContext *dc,
                                clang::DeclarationName name,
                                llvm::SmallVectorImpl<clang::NamedDecl *> &decls);
  /// True while ClangASTGenerator is synthesizing decls. The proxy uses this to
  /// avoid caching a negative lookup result that was only skipped because we
  /// were mid-generation (see FindExternalVisibleDecls), which would otherwise
  /// hide a function that is genuinely referenced later.
  bool IsGeneratingDecls() const {
    return m_generator && m_generator->IsGenerating();
  }
  void CompleteType(clang::TagDecl *tag_decl);
  void CompleteType(clang::ObjCInterfaceDecl *iface_decl);
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

  /// Provide `$__lldb_objc_class` (the Objective-C interface of the enclosing
  /// method's class) so an expression evaluated in an ObjC method can reach
  /// ivars unqualified as an access on `self`, and so a class-method self-send
  /// (`[self someOtherClassMethod]`) resolves via clang's current-method-class
  /// lookup. For an instance (`-`) method this is the pointee of the frame's
  /// implicit `self`; for a class (`+`) method `self` is a `Class` (metaclass)
  /// value instead, so the interface is looked up by name from the enclosing
  /// method's mangled `+[Class sel]` name. Mirrors
  /// ClangExpressionDeclMap::LookUpLldbObjCClass.
  void LookUpLldbObjCClass(clang::DeclarationName name,
                           llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Resolve free overloaded operator functions (e.g. `operator==`) referenced
  /// by an expression -- either explicitly (`operator==(a, b)`) or via operator
  /// syntax (`a == b`). \p name is the CXXOperatorName DeclarationName clang
  /// looked up; the target is searched for functions spelled `operator<X>` and
  /// the generated FunctionDecls carry that same operator DeclarationName so
  /// overload resolution binds to them. When \p scope_ns is non-null the lookup
  /// is qualified (`A::operator<`): only operators whose own namespace matches
  /// \p scope_ns are surfaced, and their generated decls are placed in it.
  bool
  LookupOperatorFunctions(const clang::DeclContext *dc,
                          clang::DeclarationName name,
                          llvm::SmallVectorImpl<clang::NamedDecl *> &decls,
                          const clang::NamespaceDecl *scope_ns = nullptr);

  /// The clang DeclContext a generated free-operator FunctionDecl for
  /// \p function should be placed in: the clang::NamespaceDecl matching the
  /// operator's own TypeSystemClike namespace (so argument-dependent lookup finds
  /// it for operator syntax like `a < b`), or null for an operator at global
  /// scope (which stays at translation-unit scope).
  clang::DeclContext *GetOperatorDeclContext(Function *function);

  /// Resolve free functions named \p name and generate FunctionDecls (with asm
  /// labels) for them so an expression can call them. When \p scope is a valid
  /// namespace CompilerDeclContext the search is restricted to that namespace
  /// (in \p module); otherwise the whole target is searched. Generated decls
  /// are placed in \p dc. When \p shared_seen is non-null it is used (instead of
  /// a call-local set) to dedup functions by signature, so repeated calls across
  /// several enclosing scopes collect the union of the differently-typed
  /// overloads while dropping identical-signature duplicates (an inner scope's
  /// overload hides an outer scope's same-signature one).
  ///
  /// When \p require_ns_name is non-empty the search is done target-wide and
  /// restricted to functions whose immediate enclosing namespace has that
  /// (basename) name, regardless of where that namespace is nested. This
  /// mirrors TypeSystemClang's namespace lumping: a qualified `B::func()` finds
  /// `A::B::func` even when the `B` clang resolved (e.g. a global `::B`) has no
  /// such member.
  bool LookupFunctions(const clang::DeclContext *dc, ConstString name,
                       lldb::ModuleSP module,
                       const CompilerDeclContext &scope,
                       llvm::SmallVectorImpl<clang::NamedDecl *> &decls,
                       llvm::StringSet<> *shared_seen = nullptr,
                       llvm::StringRef require_ns_name = {});

  /// Fallback for a call to a code symbol with no debug info (e.g. a libc
  /// function like `strlen`): find a code/resolver symbol named \p name and
  /// synthesize a generic variadic `__unknown_anytype ()` FunctionDecl bound to
  /// the symbol's load address (materialized via Materializer::AddSymbol). The
  /// expression must cast the call to a concrete type. Only used at
  /// translation-unit scope after every debug-info candidate (function,
  /// variable, type, namespace) failed, so a debug-info entity is never
  /// shadowed by a bare symbol.
  bool LookupSymbolFunction(const clang::DeclContext *dc, ConstString name,
                            llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Find the preferred callable code/resolver symbol named \p name in the
  /// target (an external symbol wins over a file-local one; a re-exported
  /// symbol is resolved to its target). Returns null when no callable symbol
  /// exists. Shared by LookupSymbolFunction and the module-vendor function
  /// path, both of which need the symbol's load address to lower a call.
  const Symbol *FindCallableSymbol(ConstString name);

  /// Bind a generated FunctionDecl to a target symbol so the materializer
  /// resolves the callee to the symbol's load address (used when the decl
  /// carries no FunctionCallLabel asm label of its own).
  void BindFunctionDeclToSymbol(clang::FunctionDecl *fd, const Symbol *symbol);

  /// Transport a function declared only in an imported Clang module
  /// (`@import Darwin; getpid()`) into the expression AST: query the
  /// ClangModulesDeclVendor for \p name, translate the found clang FunctionDecl
  /// type back into a TypeSystemClike FunctionType (via ClangTypeConverter over
  /// the vendor's ASTContext), synthesize a properly-typed FunctionDecl for it
  /// via ClangASTGenerator, and bind it to the callable symbol so the call can
  /// be lowered. Unlike LookupSymbolFunction this yields a real return/param
  /// signature (e.g. `getpid` returning `pid_t`) rather than
  /// `__unknown_anytype`. Only queried when a module was actually imported this
  /// session (the vendor already exists), so an ordinary symbol-only call is
  /// unaffected and no Clang module compiler is spun up spuriously.
  bool LookupModuleFunctions(const clang::DeclContext *dc, ConstString name,
                             llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Look up a data symbol named \p name that has no debug info (e.g. a global
  /// variable in a stripped/hidden translation unit) and create a VarDecl of
  /// type `void *&` bound to the symbol's load address (materialized via
  /// Materializer::AddSymbol). Only used at translation-unit scope after every
  /// debug-info candidate (variable, function, type, namespace) failed, so a
  /// debug-info entity is never shadowed by a bare symbol. Mirrors the legacy
  /// ClangExpressionDeclMap::AddOneGenericVariable path. Any ambiguity/error
  /// from choosing among conflicting symbols is reported as a parser diagnostic.
  bool LookupGlobalDataSymbol(const clang::DeclContext *dc, ConstString name,
                              llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Look up a global (or file-static) variable named \p name and create a
  /// VarDecl referencing its storage, mirroring LookupLocalVariable. When
  /// \p scope is a valid namespace the search is restricted to it (in
  /// \p module); otherwise the whole target is searched.
  bool LookupGlobalVariable(const clang::DeclContext *dc, ConstString name,
                            lldb::ModuleSP module,
                            const CompilerDeclContext &scope,
                            llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Look up a type named \p name and generate a decl for it so the expression
  /// can name it (e.g. in a cast, \c sizeof, or \c offsetof). When \p scope is
  /// a valid namespace the search is restricted to it (in \p module);
  /// otherwise the whole target is searched.
  bool LookupType(const clang::DeclContext *dc, ConstString name,
                  lldb::ModuleSP module, const CompilerDeclContext &scope,
                  llvm::SmallVectorImpl<clang::NamedDecl *> &decls);


  /// A C++ namespace named \p name can live in several modules; this is the
  /// per-module (module, namespace decl context) list the legacy path calls a
  /// "namespace map".
  using NamespaceMap =
      std::vector<std::pair<lldb::ModuleSP, CompilerDeclContext>>;

  /// If \p name names a C++ namespace visible in \p parent_dc (the translation
  /// unit for a top-level namespace, or a previously created namespace decl for
  /// a nested one), create a clang::NamespaceDecl for it in \p parent_dc (with
  /// external visible storage so member lookups call back here), cache its
  /// NamespaceMap, and push it into \p decls.
  bool LookupNamespace(const clang::DeclContext *parent_dc, ConstString name,
                       llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Materialize (or reuse) a clang::NamespaceDecl for the namespace
  /// described by \p map (its representative cpp namespace is the first
  /// entry's decl context), parented under the generator's decl context for
  /// its actual cpp parent namespace (NOT necessarily the DeclContext the
  /// lookup that found it started from -- see the comment in the .cpp file),
  /// give it external visible storage, cache its NamespaceMap, and push it
  /// into \p decls. Shared by the top-level, nested and frame-scope namespace
  /// lookups.
  bool MaterializeNamespaceMap(ConstString name, NamespaceMap map,
                               llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Resolve a name looked up inside a namespace decl \p nsd we created: fan
  /// the lookup out over the namespace's NamespaceMap (nested namespaces,
  /// types, variables, functions).
  bool LookupInNamespace(const clang::NamespaceDecl *nsd, ConstString name,
                         llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Ensure the routing map for a clang::NamespaceDecl the ClangASTGenerator
  /// created on its own (while placing a namespace-scoped type) is present in
  /// \c m_namespace_maps, so member lookups (types, functions, operators)
  /// inside it route back here. Returns false if \p nsd is not a
  /// generator-owned namespace decl or its cpp namespace can't be resolved to
  /// any module.
  bool EnsureGeneratorNamespaceMap(const clang::NamespaceDecl *nsd);

  /// Honor the current frame's enclosing C++ namespace scope for an unqualified
  /// lookup: search the frame function's namespace and its parents
  /// (innermost-first) for \p name, so an expression evaluated in `A::B::f`
  /// resolves an unqualified `x` to `A::B::x` (then `A::x`) before falling back
  /// to a global-scope search. Decls are created in \p dc (the translation
  /// unit).
  bool LookupInFrameNamespaces(const clang::DeclContext *dc, ConstString name,
                               llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Honor the current frame's enclosing C++ class scope for an unqualified
  /// lookup: if the frame function is a (possibly static) member function,
  /// resolve \p name to a static data member of the enclosing class (e.g. `s_a`
  /// to `A::s_a`). Such a member is emitted like a global but the plain global
  /// search rejects it (it must not shadow a true `::name`); this class-scoped
  /// step surfaces it. Works without a `this` pointer, so it also covers static
  /// member functions. Decls are created in \p dc (the translation unit).
  bool LookupInFrameClass(const clang::DeclContext *dc, ConstString name,
                          llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Look up a persistent expression variable (e.g. a prior result \c $0 or a
  /// user variable \c $foo) and create a reference-typed VarDecl for it.
  bool LookupPersistentVariable(const clang::DeclContext *dc, ConstString name,
                                llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Look up a persistent TYPE (e.g. \c struct $foo declared by an earlier
  /// `expression struct $foo {...};`, or a persistent typedef \c $bar from
  /// `expression typedef int $bar`) and generate a fresh decl for it in the
  /// current expression's ASTContext so it can be named again (e.g. `struct
  /// $foo $my_foo;` or `$bar i;`). Mirrors LookupType's "generate, then
  /// surface the TypedefDecl vs the TagDecl, and complete a record" dance.
  ///
  /// Also used for an ordinary (non-`$`-prefixed) class/struct/union/enum/
  /// typedef declared by an earlier *top-level* expression (`expr
  /// --top-level -- struct Foo {...};`): ClangUserExpressionHelper::
  /// CommitPersistentDecls persists every top-level TypeDecl regardless of
  /// name (only a function/variable top-level decl is excluded -- its body
  /// would need to be re-emitted into a new expression's IR every time it's
  /// referenced, which this path does not do). A plain (non-top-level)
  /// `expr struct Foo {...};` is unaffected: MaybeRecordPersistentType still
  /// only records a `$`-prefixed TypeDecl declared inside the `$__lldb_expr`
  /// body, matching TypeSystemClang. The caller (FindExternalVisibleDecls'
  /// free-name branch) only tries this after every debug-info candidate
  /// misses, so a persistent type never shadows a same-named real type/
  /// global.
  bool LookupPersistentType(const clang::DeclContext *dc, ConstString name,
                            llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  /// Fallback for a `$`-prefixed name that isn't a known persistent variable:
  /// treat it as a register name (e.g. `$arg1`, `$pc`, `$sp`) and, if the
  /// current execution context's RegisterContext knows it, create a VarDecl
  /// of a builtin type matching the register's encoding/size, bound to the
  /// register's value so the materializer can read it. Mirrors
  /// ClangExpressionDeclMap::AddOneRegister.
  bool LookupRegister(const clang::DeclContext *dc, ConstString name,
                      llvm::SmallVectorImpl<clang::NamedDecl *> &decls);

  const lldb::TargetSP m_target;
  ValueObject *m_ctx_obj;
  Materializer::PersistentVariableDelegate *m_result_delegate;
  bool m_keep_result_in_memory;
  /// When set, the `--c++-ignore-context-qualifiers` option is in effect: the
  /// synthesized `$__lldb_expr` method (and thus its implicit `this`) drops the
  /// enclosing method's cv-qualifiers, matching the unqualified out-of-line
  /// definition emitted by ClangExpressionSourceCode. This lets an expression
  /// mutate members even inside a const method.
  bool m_ignore_context_qualifiers = false;

  clang::ASTContext *m_ast_context = nullptr;
  std::optional<ClangASTGenerator> m_generator;

  /// The NamespaceMap (per-module namespace decl contexts) for each
  /// clang::NamespaceDecl we synthesized, so a member lookup inside it can be
  /// fanned out to the right modules/namespaces. Also serves as the cache that
  /// keeps a namespace decl unique per name/scope.
  llvm::DenseMap<const clang::NamespaceDecl *, NamespaceMap> m_namespace_maps;

  /// FunctionDecls generated for target functions this parse, keyed by the
  /// function's user id. A single target function can be reached by several
  /// lookups (ordinary unqualified lookup and argument-dependent lookup for the
  /// same call); returning the same FunctionDecl for each avoids synthesizing
  /// two identical-signature decls that clang would then treat as an ambiguous
  /// overload set.
  llvm::DenseMap<lldb::user_id_t, clang::FunctionDecl *> m_generated_functions;

  /// Whether a free function/variable/type/namespace lookup at TU scope is
  /// serviced at all (see the `!m_lookups_enabled` check in
  /// FindExternalVisibleDecls). On by default: an ordinary `expr ...`/`frame
  /// variable` needs every name resolved from the moment parsing starts (it
  /// has no equivalent of the "wait for a $-marker" moment the legacy
  /// ClangASTSource gate relies on -- see the comment at that check for why
  /// the gate exists at all). ClangUtilityFunctionHelper::ResetDeclMap turns
  /// this off for its self-contained utility-function decl maps, the one
  /// case that actually needs the gate.
  bool m_lookups_enabled = true;
  /// Set while LookupOperatorFunctions is running. Adding a generated operator
  /// decl into its (external-visible) namespace makes clang reconcile that
  /// name, which routes an operator lookup back here; this guard stops that
  /// reentrant lookup from recursively resolving the same operators.
  bool m_resolving_operators = false;
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

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLIKEEXPRESSIONDECLMAP_H
