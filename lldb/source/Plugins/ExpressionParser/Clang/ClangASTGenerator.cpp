//===-- ClangASTGenerator.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangASTGenerator.h"

#include "Plugins/ExpressionParser/Clang/ClangModulesDeclVendor.h"
#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "Plugins/ExpressionParser/Clang/ClangTypeConverter.h"
#include "Plugins/ExpressionParser/Clang/CppModuleHandler.h"
#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"
#include "Plugins/TypeSystem/Cpp/Builder.h"
#include "Plugins/TypeSystem/Cpp/Context.h"
#include "Plugins/TypeSystem/Cpp/Namespace.h"
#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/ModuleList.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/VTableBuilder.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/IdentifierTable.h"
#include "clang/Basic/LangOptions.h"
#include "clang/Basic/OperatorKinds.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Basic/Specifiers.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"
#include "clang/Frontend/ASTConsumers.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/ADT/StringSwitch.h"

using namespace lldb_private;
using namespace lldb;
namespace ct = cpp_typesystem;

/// Map the spelling after `operator` (e.g. `+`, `==`, `[]`, `new`) to the
/// matching clang::OverloadedOperatorKind. Returns OO_None if \p spelling is
/// not an overloadable operator token (e.g. a conversion operator like
/// `operator int`, or a plain method that merely starts with "operator"). The
/// token strings mirror clang/Basic/OperatorKinds.def.
static clang::OverloadedOperatorKind
GetOverloadedOperatorKind(llvm::StringRef spelling) {
  return llvm::StringSwitch<clang::OverloadedOperatorKind>(spelling)
      .Case("new", clang::OO_New)
      .Case("delete", clang::OO_Delete)
      .Case("new[]", clang::OO_Array_New)
      .Case("delete[]", clang::OO_Array_Delete)
      .Case("+", clang::OO_Plus)
      .Case("-", clang::OO_Minus)
      .Case("*", clang::OO_Star)
      .Case("/", clang::OO_Slash)
      .Case("%", clang::OO_Percent)
      .Case("^", clang::OO_Caret)
      .Case("&", clang::OO_Amp)
      .Case("|", clang::OO_Pipe)
      .Case("~", clang::OO_Tilde)
      .Case("!", clang::OO_Exclaim)
      .Case("=", clang::OO_Equal)
      .Case("<", clang::OO_Less)
      .Case(">", clang::OO_Greater)
      .Case("+=", clang::OO_PlusEqual)
      .Case("-=", clang::OO_MinusEqual)
      .Case("*=", clang::OO_StarEqual)
      .Case("/=", clang::OO_SlashEqual)
      .Case("%=", clang::OO_PercentEqual)
      .Case("^=", clang::OO_CaretEqual)
      .Case("&=", clang::OO_AmpEqual)
      .Case("|=", clang::OO_PipeEqual)
      .Case("<<", clang::OO_LessLess)
      .Case(">>", clang::OO_GreaterGreater)
      .Case("<<=", clang::OO_LessLessEqual)
      .Case(">>=", clang::OO_GreaterGreaterEqual)
      .Case("==", clang::OO_EqualEqual)
      .Case("!=", clang::OO_ExclaimEqual)
      .Case("<=", clang::OO_LessEqual)
      .Case(">=", clang::OO_GreaterEqual)
      .Case("<=>", clang::OO_Spaceship)
      .Case("&&", clang::OO_AmpAmp)
      .Case("||", clang::OO_PipePipe)
      .Case("++", clang::OO_PlusPlus)
      .Case("--", clang::OO_MinusMinus)
      .Case(",", clang::OO_Comma)
      .Case("->*", clang::OO_ArrowStar)
      .Case("->", clang::OO_Arrow)
      .Case("()", clang::OO_Call)
      .Case("[]", clang::OO_Subscript)
      .Case("co_await", clang::OO_Coawait)
      .Default(clang::OO_None);
}

/// Checks whether \p m1 is an overload of \p m2 (as opposed to an override).
/// Two virtual methods that merely share a name but differ in signature are
/// overloads and need distinct vtable slots; an override shares its base
/// method's slot. Mirrors TypeSystemClang::isOverload.
static bool IsOverload(clang::CXXMethodDecl *m1, clang::CXXMethodDecl *m2) {
  // FIXME: This should detect covariant return types, but currently doesn't
  // (matching TypeSystemClang).
  clang::ASTContext &context = m1->getASTContext();
  const auto *m1Type = llvm::cast<clang::FunctionProtoType>(
      context.getCanonicalType(m1->getType()));
  const auto *m2Type = llvm::cast<clang::FunctionProtoType>(
      context.getCanonicalType(m2->getType()));

  auto compareArgTypes = [&context](const clang::QualType &m1p,
                                    const clang::QualType &m2p) {
    return context.hasSameType(m1p.getUnqualifiedType(),
                               m2p.getUnqualifiedType());
  };

  return (m1->getNumParams() != m2->getNumParams()) ||
         !std::equal(m1Type->param_type_begin(), m1Type->param_type_end(),
                     m2Type->param_type_begin(), compareArgTypes);
}

/// If \p decl is a virtual method, walk the base classes looking for methods it
/// overrides and record them via addOverriddenMethod. Clang's IRGen relies on
/// this override table to compute the vtable layout for decl's parent class: an
/// overriding method reuses the base method's vtable slot rather than getting a
/// new one. Without it a call through a derived object dispatches through the
/// wrong (out-of-range) slot and crashes. Mirrors
/// TypeSystemClang::addOverridesForMethod, which the DWARF-fed Clang AST relies
/// on for the same reason.
static void AddOverridesForMethod(clang::CXXMethodDecl *decl) {
  if (!decl->isVirtual())
    return;

  clang::CXXBasePaths paths;
  llvm::SmallVector<clang::NamedDecl *, 4> decls;

  auto find_overridden_methods =
      [&decls, decl](const clang::CXXBaseSpecifier *specifier,
                     clang::CXXBasePath &path) {
        if (auto *base_record = specifier->getType()->getAsCXXRecordDecl()) {
          // lookupInBases recurses through the whole base hierarchy, including
          // bases we haven't populated a definition for yet. Querying members
          // (getDestructor/lookup) of a class with no definition asserts in
          // clang, so skip any base that isn't complete. A base that a derived
          // method actually overrides was completed by PopulateRecord before
          // this runs, so this only skips unrelated incomplete bases.
          if (!base_record->hasDefinition())
            return false;

          clang::DeclarationName name = decl->getDeclName();

          // A destructor overrides the base destructor iff the base one is
          // virtual (destructors don't match by name/signature otherwise).
          if (name.getNameKind() == clang::DeclarationName::CXXDestructorName) {
            if (auto *baseDtorDecl = base_record->getDestructor()) {
              if (baseDtorDecl->isVirtual()) {
                decls.push_back(baseDtorDecl);
                return true;
              }
              return false;
            }
          }

          // Otherwise, search for the name in the base class.
          for (path.Decls = base_record->lookup(name).begin();
               path.Decls != path.Decls.end(); ++path.Decls) {
            if (auto *method_decl =
                    llvm::dyn_cast<clang::CXXMethodDecl>(*path.Decls))
              if (method_decl->isVirtual() && !IsOverload(decl, method_decl)) {
                decls.push_back(method_decl);
                return true;
              }
          }
        }
        return false;
      };

  if (decl->getParent()->lookupInBases(find_overridden_methods, paths))
    for (auto *overridden_decl : decls)
      decl->addOverriddenMethod(
          llvm::cast<clang::CXXMethodDecl>(overridden_decl));
}

/// Records a (clang type -> cpp type) mapping so the type can later be mapped
/// back onto a TypeSystemCpp type (e.g. an expression result type). The key is
/// the opaque QualType so cv-qualified variants (const int vs int) stay
/// distinct. Also register the canonical type as a fallback so a desugared
/// variant the parser formed still maps back -- but only when the type is
/// itself canonical (unsugared). Registering the canonical alias for a sugared
/// type would be wrong: a generated typedef (e.g. `int32_t`, whose canonical
/// type is `int`) would claim the canonical `int` mapping, and an unrelated
/// `int` result would then be reported as `int32_t`.
///
/// Also records \p cpp_type's true owning TypeSystemCpp (\p ts, the same one
/// GenerateType was called with for it) in \p owner, so ConvertViaReverseMap
/// can later rebuild the CompilerType against the TypeSystem that can actually
/// complete it, instead of always the scratch TypeSystemCpp that owns
/// expression result types.
static void
noteReverse(llvm::DenseMap<void *, ct::Type *> &reverse,
           llvm::DenseMap<ct::Type *, TypeSystemCpp *> &owner,
           TypeSystemCpp &ts, clang::QualType qt, ct::Type *cpp_type) {
  if (qt.isNull())
    return;
  reverse[qt.getAsOpaquePtr()] = cpp_type;
  owner[cpp_type] = &ts;
  clang::QualType canonical = qt.getCanonicalType();
  if (canonical.getAsOpaquePtr() == qt.getAsOpaquePtr())
    reverse[canonical.getAsOpaquePtr()] = cpp_type;
}

/// Peel typedef/cv-qualifier "sugar" off a cpp_typesystem type to reach its
/// canonical type. A pointee reached through a typedef (e.g. `typedef
/// BaseClass TypedefBaseClass; TypedefBaseClass *p;`) must still be recognized
/// as, say, an Objective-C interface so `p` is generated as a real
/// ObjCObjectPointerType rather than a plain pointer.
static ct::Type *Desugar(ct::Type *t) {
  while (auto *sugar = llvm::dyn_cast_or_null<ct::SugarType>(t))
    t = sugar->GetUnderlyingType();
  return t;
}

void ClangASTGenerator::RegisterNamespace(const ct::Namespace *cpp_ns,
                                          clang::NamespaceDecl *clang_ns) {
  if (cpp_ns)
    m_namespaces[cpp_ns] = clang_ns;
}

clang::NamespaceDecl *
ClangASTGenerator::GetRegisteredNamespace(const ct::Namespace *cpp_ns) const {
  if (!cpp_ns)
    return nullptr;
  auto it = m_namespaces.find(cpp_ns);
  return it != m_namespaces.end() ? it->second : nullptr;
}

const ct::Namespace *
ClangASTGenerator::GetNamespaceForDecl(const clang::NamespaceDecl *clang_ns) const {
  auto it = m_namespace_decls.find(clang_ns);
  return it != m_namespace_decls.end() ? it->second : nullptr;
}

clang::DeclContext *
ClangASTGenerator::GetDeclContextForNamespace(const ct::Namespace *cpp_ns) {
  if (!cpp_ns)
    return m_ast.getTranslationUnitDecl();

  auto it = m_namespaces.find(cpp_ns);
  if (it != m_namespaces.end())
    return clang::Decl::castToDeclContext(it->second);

  // No clang::NamespaceDecl exists yet for this cpp namespace. Materialize the
  // whole enclosing chain (parent-first) so that a type declared in `A::B` is
  // nested inside real NamespaceDecls rather than dumped into the translation
  // unit. Nesting matters for name lookup: it keeps a namespace-scoped type
  // (e.g. `ns::Foo`) out of the global scope, so a global-qualified lookup
  // (`::Foo`) does not spuriously find it, and clang's own unqualified lookup
  // walks the enclosing namespaces itself.
  clang::DeclContext *parent = GetDeclContextForNamespace(cpp_ns->GetParent());
  clang::IdentifierInfo *ident = nullptr;
  if (!cpp_ns->IsAnonymous())
    ident = &m_ast.Idents.get(cpp_ns->GetName().GetName());

  // A real C++ module (`@import std;`, see CxxModuleHandler/
  // target.import-std-module) may have already materialized a namespace of
  // this name directly into `parent` -- e.g. `std`, referenced both by a
  // debug-info type (`std::vector<int>`) and by code the imported module
  // itself defines. Reuse that namespace rather than create a second,
  // unrelated NamespaceDecl of the same name: two independent decls make
  // Sema report an unqualified reference to the name as ambiguous (see
  // CppModuleHandler).
  if (ident) {
    if (clang::NamespaceDecl *existing =
            CppModuleHandler::FindImportedNamespace(
                parent, cpp_ns->GetName().GetName())) {
      clang::Decl::castToDeclContext(existing)->setHasExternalVisibleStorage(
          true);
      RegisterNamespace(cpp_ns, existing);
      m_namespace_decls[existing] = cpp_ns;
      return clang::Decl::castToDeclContext(existing);
    }
  }

  auto *nsd = clang::NamespaceDecl::Create(
      m_ast, parent, cpp_ns->IsInline(), clang::SourceLocation(),
      clang::SourceLocation(), ident, /*PrevDecl=*/nullptr, /*Nested=*/false);
  // Route member lookups (qualified names, ADL operators) inside this namespace
  // back through the decl map: without external visible storage a qualified
  // lookup such as `A::operator<` would find nothing, since the operator is
  // resolved lazily and never lives lexically in the decl. Record the reverse
  // mapping so the decl map can rebuild this namespace's lookup map on demand.
  clang::Decl::castToDeclContext(nsd)->setHasExternalVisibleStorage(true);
  parent->addDecl(nsd);
  RegisterNamespace(cpp_ns, nsd);
  m_namespace_decls[nsd] = cpp_ns;
  return clang::Decl::castToDeclContext(nsd);
}

clang::QualType ClangASTGenerator::Generate(const CompilerType &cpp_type) {
  GenerationGuard guard(*this);
  if (!cpp_type)
    return {};
  auto ts = cpp_type.GetTypeSystem<TypeSystemCpp>();
  if (!ts)
    return {};
  auto *type = static_cast<ct::Type *>(cpp_type.GetOpaqueQualType());
  if (!type)
    return {};
  // This is the type directly requested by the expression parser, so build a
  // full template specialization decl for it if it is a template-id (needed for
  // template-id name lookup). Everything reached transitively from here stays
  // lazy (build_template_spec defaults to false).
  return GenerateType(*ts, type, /*build_template_spec=*/true);
}

void ClangASTGenerator::DumpRecords(TypeSystemCpp &ts,
                                    const llvm::Triple &triple,
                                    llvm::ArrayRef<CompilerType> records,
                                    llvm::raw_ostream &output,
                                    llvm::StringRef filter, bool show_color) {
  // Build a throwaway clang::ASTContext for the module's target. Mirrors
  // TypeSystemClang::CreateASTContext(); everything here is owned locally and
  // torn down when this function returns.
  clang::LangOptions lang_opts;
  lang_opts.CPlusPlus = true;
  lang_opts.CPlusPlus11 = true;

  clang::IdentifierTable idents(lang_opts, nullptr);
  clang::Builtin::Context builtins;
  clang::SelectorTable selectors;

  clang::FileSystemOptions file_system_options;
  clang::FileManager file_manager(
      file_system_options, FileSystem::Instance().GetVirtualFileSystem());

  auto diag_options = std::make_shared<clang::DiagnosticOptions>();
  clang::DiagnosticsEngine diagnostics(clang::DiagnosticIDs::create(),
                                       *diag_options);
  clang::SourceManager source_manager(diagnostics, file_manager);

  clang::ASTContext ast(lang_opts, source_manager, idents, selectors, builtins,
                        clang::TranslationUnitKind::TU_Complete);
  ast.getDiagnostics().getDiagnosticOptions().setShowColors(
      show_color ? clang::ShowColorsKind::On : clang::ShowColorsKind::Off);

  auto target_options = std::make_shared<clang::TargetOptions>();
  target_options->Triple = triple.str();
  if (clang::TargetInfo *target_info = clang::TargetInfo::CreateTargetInfo(
          ast.getDiagnostics(), *target_options))
    ast.InitBuiltinTypes(*target_info);

  // Synthesize a definition for each record into the throwaway context.
  // Generate() only hands out a forward declaration (completion is normally
  // driven lazily by clang's external source, which this standalone context
  // has none of), so complete each record explicitly -- but ONLY the records
  // that are already complete in the cpp_typesystem model. A record parsed
  // from DWARF stays a forward declaration until something actually needs its
  // definition (lazy completion); a record reachable only through a pointer or
  // reference member is never completed. Force-completing every record here
  // (EnsureComplete -> GetCompleteType) would defeat that laziness and load
  // every referenced type into the dump, which is exactly what the
  // lazy-loading test guards against. Checking ct::RecordType::IsComplete()
  // reads the record's current completeness flag without forcing it.
  ClangASTGenerator generator(ast);
  for (const CompilerType &record : records) {
    clang::QualType qt = generator.Generate(record);
    ct::Type *cpp_type =
        TypeSystemCpp::GetCppType(record.GetOpaqueQualType());
    auto *cpp_rec = llvm::dyn_cast_or_null<ct::RecordType>(cpp_type);
    if (cpp_rec && cpp_rec->IsComplete())
      generator.EnsureComplete(qt);
  }

  // Completing a record only forward-declares the records it points to (a
  // pointer/reference member needs no definition). Those still-incomplete
  // decls kept the external-storage flags set in GenerateType, but this
  // standalone context has no external source to satisfy them -- the AST
  // dumper would assert when it tried to load their lexical decls. A *completed*
  // record with nested types also keeps external visible storage on (so the
  // expression path can route `Record::Nested` lookups back through the decl
  // map), but here every member is already materialized and there is no
  // external source, so a lookup into it (e.g. the dumper's implicit
  // getDestructor()) would likewise assert. Clear both flags on every completed
  // record so the dumper treats each as fully self-contained.
  //
  // A record left as a forward declaration (info.completed == false: never
  // populated because its cpp record is still incomplete -- e.g. it is only
  // reachable through a pointer/reference and lazy completion never touched it)
  // is a different case. It has no fields/bases and no lookups, so the dumper
  // will not deserialize anything for it; keeping HasExternalLexicalStorage set
  // makes the dumper print it with a "<undeserialized declarations>" marker
  // instead of as a bare (and thus apparently "completed") empty struct. This
  // mirrors how TypeSystemClang renders its lazily-loaded forward declarations
  // and is what the lazy-loading test uses to distinguish a loaded-but-not-
  // completed decl from a fully completed one.
  for (auto &entry : generator.m_records) {
    clang::CXXRecordDecl *decl = entry.second->clang_decl;
    decl->setHasExternalVisibleStorage(false);
    if (entry.second->completed)
      decl->setHasExternalLexicalStorage(false);
  }

  std::unique_ptr<clang::ASTConsumer> consumer = clang::CreateASTDumper(
      output, filter, /*DumpDecls=*/true, /*Deserialize=*/false,
      /*DumpLookups=*/false, /*DumpDeclTypes=*/false, clang::ADOF_Default);
  consumer->HandleTranslationUnit(ast);
}

std::optional<uint64_t> ClangASTGenerator::ComputeVBaseOffsetOffset(
    TypeSystemCpp &ts, const llvm::Triple &triple, const CompilerType &derived,
    const CompilerType &vbase) {
  // Build a throwaway clang::ASTContext for the target, exactly as DumpRecords
  // does. Everything here is owned locally and torn down on return.
  clang::LangOptions lang_opts;
  lang_opts.CPlusPlus = true;
  lang_opts.CPlusPlus11 = true;

  clang::IdentifierTable idents(lang_opts, nullptr);
  clang::Builtin::Context builtins;
  clang::SelectorTable selectors;

  clang::FileSystemOptions file_system_options;
  clang::FileManager file_manager(
      file_system_options, FileSystem::Instance().GetVirtualFileSystem());

  auto diag_options = std::make_shared<clang::DiagnosticOptions>();
  clang::DiagnosticsEngine diagnostics(clang::DiagnosticIDs::create(),
                                       *diag_options);
  clang::SourceManager source_manager(diagnostics, file_manager);

  clang::ASTContext ast(lang_opts, source_manager, idents, selectors, builtins,
                        clang::TranslationUnitKind::TU_Complete);

  auto target_options = std::make_shared<clang::TargetOptions>();
  target_options->Triple = triple.str();
  clang::TargetInfo *target_info =
      clang::TargetInfo::CreateTargetInfo(ast.getDiagnostics(), *target_options);
  if (!target_info)
    return std::nullopt;
  ast.InitBuiltinTypes(*target_info);

  // Synthesize + fully lay out the derived record. EnsureComplete completes its
  // bases (including the virtual base) too, so clang can compute the Itanium
  // vtable layout. LayoutRecord deliberately omits virtual bases from the base
  // offsets it reports, so clang lays them out itself -- which is exactly the
  // vtable layout we want to query below.
  ClangASTGenerator generator(ast);
  clang::QualType derived_qt = generator.Generate(derived);
  if (derived_qt.isNull())
    return std::nullopt;
  generator.EnsureComplete(derived_qt);

  // The vbase decl must live in the same context; generating it just returns
  // the record already created while completing the derived record (identity is
  // keyed on the cpp record), so this does not create a second decl.
  clang::QualType vbase_qt = generator.Generate(vbase);
  if (vbase_qt.isNull())
    return std::nullopt;

  const auto *derived_decl = derived_qt->getAsCXXRecordDecl();
  const auto *vbase_decl = vbase_qt->getAsCXXRecordDecl();
  if (!derived_decl || !vbase_decl || !derived_decl->isCompleteDefinition())
    return std::nullopt;
  // A vtable-relative vbase offset only exists when the derived type has a
  // vtable. A class with a virtual base is a "dynamic class" (it needs a vtable
  // to store the vbase offset) even if it declares no virtual member functions,
  // so query isDynamicClass() rather than isPolymorphic().
  if (!derived_decl->isDynamicClass())
    return std::nullopt;

  clang::VTableContextBase *vtable_ctx = ast.getVTableContext();
  if (!vtable_ctx || vtable_ctx->isMicrosoft())
    return std::nullopt;
  auto &itanium = static_cast<clang::ItaniumVTableContext &>(*vtable_ctx);

  clang::CharUnits ooo =
      itanium.getVirtualBaseOffsetOffset(derived_decl, vbase_decl);
  // getVirtualBaseOffsetOffset returns a negative offset from the vtable
  // pointer; TypeSystemCpp's ReadVirtualBaseOffset subtracts a positive value.
  int64_t q = ooo.getQuantity();
  if (q >= 0)
    return std::nullopt;
  return static_cast<uint64_t>(-q);
}

clang::QualType ClangASTGenerator::GenerateBuiltin(ct::Type *cpp_type) {
  clang::ASTContext &ast = m_ast;
  llvm::StringRef name = cpp_type->GetName().GetName();

  // Match well-known spellings first so that same-sized types (e.g. `long` vs
  // `long long`) map to the right Clang builtin.
  clang::QualType by_name =
      llvm::StringSwitch<clang::QualType>(name)
          .Case("void", ast.VoidTy)
          .Case("bool", ast.BoolTy)
          .Case("char", ast.CharTy)
          .Case("signed char", ast.SignedCharTy)
          .Case("unsigned char", ast.UnsignedCharTy)
          .Case("wchar_t", ast.WCharTy)
          .Case("char8_t", ast.Char8Ty)
          .Case("char16_t", ast.Char16Ty)
          .Case("char32_t", ast.Char32Ty)
          .Case("short", ast.ShortTy)
          .Case("short int", ast.ShortTy)
          .Case("unsigned short", ast.UnsignedShortTy)
          .Case("short unsigned int", ast.UnsignedShortTy)
          .Case("int", ast.IntTy)
          .Case("unsigned int", ast.UnsignedIntTy)
          .Case("unsigned", ast.UnsignedIntTy)
          .Case("long", ast.LongTy)
          .Case("long int", ast.LongTy)
          .Case("unsigned long", ast.UnsignedLongTy)
          .Case("long unsigned int", ast.UnsignedLongTy)
          .Case("long long", ast.LongLongTy)
          .Case("long long int", ast.LongLongTy)
          .Case("unsigned long long", ast.UnsignedLongLongTy)
          .Case("long long unsigned int", ast.UnsignedLongLongTy)
          .Case("float", ast.FloatTy)
          .Case("double", ast.DoubleTy)
          .Case("long double", ast.LongDoubleTy)
          .Case("__int128", ast.Int128Ty)
          .Case("unsigned __int128", ast.UnsignedInt128Ty)
          .Default(clang::QualType());
  if (!by_name.isNull())
    return by_name;

  // Fall back to the encoding + byte size.
  uint64_t bytes = cpp_type->GetByteSize().value_or(0);
  switch (cpp_type->GetEncoding()) {
  case eEncodingSint:
    if (bytes == 1)
      return ast.SignedCharTy;
    if (bytes == 2)
      return ast.ShortTy;
    if (bytes == 4)
      return ast.IntTy;
    if (bytes == 8)
      return ast.LongLongTy;
    break;
  case eEncodingUint:
    if (bytes == 1)
      return ast.UnsignedCharTy;
    if (bytes == 2)
      return ast.UnsignedShortTy;
    if (bytes == 4)
      return ast.UnsignedIntTy;
    if (bytes == 8)
      return ast.UnsignedLongLongTy;
    break;
  case eEncodingIEEE754:
    if (bytes == 4)
      return ast.FloatTy;
    if (bytes == 8)
      return ast.DoubleTy;
    if (bytes == 16)
      return ast.LongDoubleTy;
    break;
  default:
    break;
  }
  return {};
}

clang::CXXRecordDecl *ClangASTGenerator::BuildClassTemplateSpecializationDecl(
    TypeSystemCpp &ts, ct::RecordType *rec, clang::TagTypeKind kind,
    clang::DeclContext *decl_ctx, llvm::StringRef base_name) {
  clang::ASTContext &ast = m_ast;
  if (base_name.empty())
    return nullptr;

  // Build the clang template arguments (and matching template parameters) from
  // the modeled cpp template arguments. Mirrors TypeSystemClang's
  // CreateTemplateParameterList / CreateClassTemplateSpecializationDecl but
  // driven by cpp_typesystem::TemplateArgument. Type arguments become
  // TemplateTypeParmDecls, integral arguments NonTypeTemplateParmDecls. Any
  // argument kind we can't model (a template-template argument, whose modeled
  // type we don't have) makes us bail so the caller falls back to a plain
  // record decl.
  llvm::SmallVector<clang::TemplateArgument, 4> args;
  llvm::SmallVector<clang::NamedDecl *, 4> param_decls;
  clang::DeclContext *tu = ast.getTranslationUnitDecl();
  const unsigned depth = 0;
  for (uint32_t i = 0; i < rec->GetNumTemplateArguments(); ++i) {
    const ct::TemplateArgument *arg = rec->GetTemplateArgumentAtIndex(i);
    if (!arg)
      return nullptr;
    if (arg->kind == lldb::eTemplateArgumentKindType) {
      // A type argument only needs to be a type name in the specialization; it
      // does not have to be a complete specialization decl itself. Generating
      // it lazily (build_template_spec=false) stops a nested template argument
      // (e.g. the `SmallVector<V>` in `DenseMap<K, SmallVector<V>>`) from being
      // force-completed just to name it here -- which would otherwise cascade
      // through the whole template-argument graph.
      clang::QualType arg_qt =
          GenerateType(ts, arg->type.Get(), /*build_template_spec=*/false);
      if (arg_qt.isNull())
        return nullptr;
      args.push_back(clang::TemplateArgument(arg_qt));
      param_decls.push_back(clang::TemplateTypeParmDecl::Create(
          ast, tu, clang::SourceLocation(), clang::SourceLocation(), depth, i,
          /*Id=*/nullptr, /*Typename=*/false, /*ParameterPack=*/false));
    } else if (arg->kind == lldb::eTemplateArgumentKindIntegral) {
      clang::QualType arg_qt = GenerateType(ts, arg->type.Get());
      if (arg_qt.isNull() || !arg_qt->isIntegralOrEnumerationType())
        return nullptr;
      ct::Type *arg_type = arg->type.Get();
      const bool is_signed =
          arg_type && arg_type->GetEncoding() == lldb::eEncodingSint;
      unsigned width = ast.getIntWidth(arg_qt);
      llvm::APSInt value(llvm::APInt(width, arg->integral_value, is_signed),
                         !is_signed);
      args.push_back(clang::TemplateArgument(ast, value, arg_qt));
      param_decls.push_back(clang::NonTypeTemplateParmDecl::Create(
          ast, tu, clang::SourceLocation(), clang::SourceLocation(), depth, i,
          /*Id=*/nullptr, arg_qt, /*ParameterPack=*/false,
          ast.getTrivialTypeSourceInfo(arg_qt)));
    } else {
      return nullptr;
    }
  }

  clang::IdentifierInfo &ii = ast.Idents.get(base_name);
  clang::DeclarationName decl_name(&ii);

  clang::TemplateParameterList *param_list =
      clang::TemplateParameterList::Create(
          ast, clang::SourceLocation(), clang::SourceLocation(), param_decls,
          clang::SourceLocation(), /*RequiresClause=*/nullptr);

  // The bare template's pattern record (a forward declaration; a class template
  // has "specializations" but the pattern itself is never defined here).
  auto *pattern = clang::CXXRecordDecl::CreateDeserialized(
      ast, clang::GlobalDeclID());
  pattern->setTagKind(kind);
  pattern->setDeclContext(decl_ctx);
  pattern->setDeclName(decl_name);
  for (clang::NamedDecl *param : param_decls)
    param->setDeclContext(pattern);

  auto *class_template =
      clang::ClassTemplateDecl::CreateDeserialized(ast, clang::GlobalDeclID());
  class_template->setDeclContext(decl_ctx);
  class_template->setDeclName(decl_name);
  class_template->setTemplateParameters(param_list);
  class_template->init(pattern);
  pattern->setDescribedClassTemplate(class_template);
  class_template->setAccess(clang::AS_public);
  decl_ctx->addDecl(class_template);

  auto *spec = clang::ClassTemplateSpecializationDecl::CreateDeserialized(
      ast, clang::GlobalDeclID());
  spec->setTagKind(kind);
  spec->setDeclContext(decl_ctx);
  spec->setInstantiationOf(class_template);
  spec->setTemplateArgs(clang::TemplateArgumentList::CreateCopy(ast, args));
  void *insert_pos = nullptr;
  if (!class_template->findSpecialization(args, insert_pos))
    class_template->AddSpecialization(spec, insert_pos);
  spec->setDeclName(decl_name);
  spec->setStrictPackMatch(false);
  spec->setSpecializationKind(clang::TSK_ExplicitSpecialization);
  spec->setAccess(clang::AS_public);
  decl_ctx->addDecl(spec);

  // Completed lazily like every other generated record (see PopulateRecord).
  spec->setHasExternalLexicalStorage(true);
  spec->setHasExternalVisibleStorage(true);
  return spec;
}

clang::QualType ClangASTGenerator::GenerateType(TypeSystemCpp &ts,
                                                ct::Type *cpp_type,
                                                bool build_template_spec) {
  if (!cpp_type)
    return {};

  // Return the cached translation if we already generated this type. This also
  // breaks cycles (e.g. a record that transitively points back to itself).
  auto cached = m_generated.find(cpp_type);
  if (cached != m_generated.end())
    return clang::QualType::getFromOpaquePtr(cached->second);

  Log *log = GetLog(LLDBLog::Expressions);
  clang::ASTContext &ast = m_ast;
  clang::QualType result;

  // A named type (record/enum/typedef) is placed inside the clang
  // NamespaceDecl matching its cpp declaration context (if the decl map has
  // created one), so its qualified name mangles correctly for the JIT and
  // qualified lookups (`A::B::Bar`) resolve. Unnamespaced types fall back to
  // the translation unit. The declared name is the *unqualified* spelling
  // (the record's GetName() is the fully-qualified one).
  clang::DeclContext *decl_ctx = GetDeclContextForNamespace(
      cpp_type->GetDeclContext());
  auto unqualified_name = [&](ct::Type *t) -> llvm::StringRef {
    llvm::StringRef n = t->GetUnqualifiedName().GetName();
    return n.empty() ? t->GetName().GetName() : n;
  };

  if (auto *iface = llvm::dyn_cast<ct::ObjCInterfaceType>(cpp_type)) {
    // An Objective-C class must become a clang ObjCInterfaceDecl (not a
    // CXXRecordDecl): its ivars are laid out with the Objective-C runtime ABI,
    // and modeling it as a C++ record misclassifies its runtime-offset bitfield
    // ivars, tripping checkBitfieldClipping during expression codegen. Created
    // as a forward declaration and completed on demand via
    // CompleteObjCInterface (mirroring how records are completed).
    llvm::StringRef name = unqualified_name(iface);
    // Unify by name (like records, see m_records_by_name below): reuse an
    // existing clang decl so a class arriving from the runtime, an imported
    // module, and plain debug info all map to a single clang::ObjCInterfaceDecl
    // -- otherwise a name lookup finds several same-named interfaces
    // ("ambiguous") and `NSString *` from two sources are distinct types.
    if (!name.empty()) {
      auto by_name = m_objc_interfaces_by_name.find(name);
      if (by_name != m_objc_interfaces_by_name.end()) {
        result = ast.getObjCInterfaceType(by_name->second);
        m_generated[cpp_type] = result.getAsOpaquePtr();
        // Leave m_reverse pointing at the cpp type the decl was first generated
        // for (its ObjCInterfaceInfo drives completion), like the record path.
        return result;
      }
    }
    auto *decl =
        clang::ObjCInterfaceDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    decl->setDeclContext(decl_ctx);
    if (!name.empty())
      decl->setDeclName(&ast.Idents.get(name));
    decl_ctx->addDecl(decl);
    // Ask clang to call back into us (CompleteType(ObjCInterfaceDecl*)) before
    // it needs the definition.
    decl->setHasExternalLexicalStorage(true);
    decl->setHasExternalVisibleStorage(true);

    result = ast.getObjCInterfaceType(decl);
    auto info = std::make_unique<ObjCInterfaceInfo>();
    info->ts = &ts;
    info->cpp_iface = iface;
    info->clang_decl = decl;
    m_objc_interfaces[decl] = std::move(info);
    if (!name.empty())
      m_objc_interfaces_by_name[name] = decl;
  } else if (auto *rec = llvm::dyn_cast<ct::RecordType>(cpp_type)) {
    // Records are created as forward declarations and completed on demand (see
    // PopulateRecord). This mirrors lazy DWARF parsing and keeps cycles finite.
    clang::TagTypeKind kind = rec->IsUnion()
                                  ? clang::TagTypeKind::Union
                                  : (rec->IsClassKeyword()
                                         ? clang::TagTypeKind::Class
                                         : clang::TagTypeKind::Struct);
    // A class-template instantiation must be modeled as a
    // ClassTemplateSpecializationDecl (backed by a ClassTemplateDecl named by
    // the bare template name), so a template-id written in an expression
    // (`TestObj<int>`) resolves: the parser looks up the template name and
    // applies the arguments. Whether a record is a template instantiation --
    // and its template arguments -- is only known once the record is completed
    // (both come from the same DWARF completion step). Completing every record
    // here would defeat lazy completion (a record reachable only through a
    // pointer must stay a forward declaration), so first use the record's name
    // as a cheap, completion-free filter: a template instantiation's spelling
    // is a template-id (`TestObj<int>`), which contains a `<` and is never a
    // plain identifier; a non-template record's name never does. The
    // fully-qualified name (GetName) always carries the reconstructed `<...>`
    // even under -gsimple-template-names, whereas the unqualified spelling may
    // be the bare `TestObj`. Only for a template-id name do we complete the
    // record to read its template arguments and build the specialization decl.
    // Look at the record's own (last) name component so a `<` that belongs only
    // to an enclosing scope (`Foo<int>::Bar`) doesn't misfire.
    llvm::StringRef name = unqualified_name(rec);
    llvm::StringRef full_name = rec->GetName().GetName();
    llvm::StringRef last_component = full_name;
    if (size_t scope = full_name.rfind("::"); scope != llvm::StringRef::npos)
      last_component = full_name.substr(scope + 2);
    if (build_template_spec &&
        (name.contains('<') || last_component.contains('<'))) {
      ts.GetCompilerType(rec).GetCompleteType();
      if (rec->IsTemplateInstantiation()) {
        llvm::StringRef base_name = name.substr(0, name.find('<'));
        if (clang::CXXRecordDecl *spec = BuildClassTemplateSpecializationDecl(
                ts, rec, kind, decl_ctx, base_name)) {
          result = ast.getCanonicalTagType(spec);
          auto info = std::make_unique<RecordInfo>();
          info->ts = &ts;
          info->cpp_record = rec;
          info->clang_decl = spec;
          m_records[spec] = std::move(info);
          m_generated[cpp_type] = result.getAsOpaquePtr();
          m_reverse[result.getAsOpaquePtr()] = cpp_type;
          m_type_owner[cpp_type] = &ts;
          return result;
        }
      }
    }
    // A named record is unified by its fully-qualified name: if we already
    // generated a clang decl for a record of this name (from any module's
    // TypeSystemCpp), reuse it rather than create a second, distinct decl. This
    // keeps a type that is forward-declared in one module and defined in
    // another (a shared-library type used in the main executable) a single
    // clang type, so overload resolution binds `foo *` from a variable and
    // `foo *` from a function parameter to the same type. Only named records
    // participate (an unnamed / expression-local record can't collide).
    if (!full_name.empty()) {
      auto by_name = m_records_by_name.find(full_name);
      if (by_name != m_records_by_name.end()) {
        result = clang::QualType::getFromOpaquePtr(by_name->second);
        m_generated[cpp_type] = result.getAsOpaquePtr();
        // Do not overwrite m_reverse: leave it pointing at the cpp type the
        // decl was first generated for (its RecordInfo drives completion).
        return result;
      }
    }

    auto *decl =
        clang::CXXRecordDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    decl->setTagKind(kind);
    decl->setDeclContext(decl_ctx);
    if (!name.empty())
      decl->setDeclName(&ast.Idents.get(name));
    decl->setAccess(clang::AS_public);
    decl_ctx->addDecl(decl);
    // Ask Clang to call back into us (CompleteType) before it needs the
    // definition.
    decl->setHasExternalLexicalStorage(true);
    decl->setHasExternalVisibleStorage(true);

    result = ast.getCanonicalTagType(decl);
    auto info = std::make_unique<RecordInfo>();
    info->ts = &ts;
    info->cpp_record = rec;
    info->clang_decl = decl;
    m_records[decl] = std::move(info);
    if (!full_name.empty())
      m_records_by_name[full_name] = result.getAsOpaquePtr();
  } else if (auto *ptr = llvm::dyn_cast<ct::PointerType>(cpp_type)) {
    clang::QualType pointee;
    if (ct::Type *p = ptr->GetPointeeType())
      // A pointee stays a lazy forward declaration: don't force-complete it
      // (and don't build a template specialization decl for it) just to form
      // the pointer type. See GenerateType's build_template_spec doc.
      pointee = GenerateType(ts, p, /*build_template_spec=*/false);
    else
      pointee = ast.VoidTy;
    if (!pointee.isNull()) {
      // A pointer to an Objective-C class (`Foo *`) must be a clang
      // ObjCObjectPointerType, not a plain PointerType, so member access
      // (`obj->ivar` / `obj.prop`) and messaging resolve through the ObjC path.
      // The pointee may be reached through a typedef (`typedef BaseClass
      // TypedefBaseClass; TypedefBaseClass *`), so desugar before checking --
      // otherwise the pointer stays a plain PointerType and `.`/`->` member
      // access fails with "not a structure or union".
      if (llvm::isa_and_nonnull<ct::ObjCInterfaceType>(
              Desugar(ptr->GetPointeeType())) &&
          pointee->isObjCObjectType())
        result = ast.getObjCObjectPointerType(pointee);
      // An Apple "blocks" pointer (`int (^)(int)`) wraps a function type but
      // must be a real clang BlockPointerType so it stays callable and prints
      // with `^` rather than `*`.
      else if (llvm::isa<ct::BlockPointerType>(ptr))
        result = ast.getBlockPointerType(pointee);
      else
        result = ast.getPointerType(pointee);
    }
  } else if (auto *ref = llvm::dyn_cast<ct::ReferenceType>(cpp_type)) {
    // Like a pointer pointee, a reference's referent stays a lazy forward
    // declaration (completed on demand if actually dereferenced).
    clang::QualType pointee =
        GenerateType(ts, ref->GetPointeeType(), /*build_template_spec=*/false);
    if (!pointee.isNull())
      result = ref->IsRValue() ? ast.getRValueReferenceType(pointee)
                               : ast.getLValueReferenceType(pointee);
  } else if (auto *arr = llvm::dyn_cast<ct::ArrayType>(cpp_type)) {
    clang::QualType elem = GenerateType(ts, arr->GetElementType());
    if (!elem.isNull()) {
      std::optional<uint64_t> n = arr->GetNumElements();
      if (arr->IsVector() && n) {
        // A GCC/Clang vector type (e.g. ext_vector_type) must be reproduced as a
        // clang vector type, not a plain array, so vector-format reinterpretation
        // (e.g. `expr -f int16_t[] -- v`) sees the right number of elements.
        result = ast.getExtVectorType(elem, *n);
      } else if (n) {
        result = ast.getConstantArrayType(
            elem, llvm::APInt(64, *n), nullptr,
            clang::ArraySizeModifier::Normal, 0);
      } else
        result = ast.getIncompleteArrayType(
            elem, clang::ArraySizeModifier::Normal, 0);
    }
  } else if (auto *td = llvm::dyn_cast<ct::TypedefType>(cpp_type)) {
    // The Objective-C pseudo-builtin `id` appears in DWARF as an ordinary
    // typedef (`id` -> `objc_object *`), but clang's ObjC semantics (implicit
    // conversions such as `NSString*` -> `id`, and message sends to `id`) only
    // apply through the special built-in `id` type. Map it back to the
    // ASTContext's canonical builtin so those semantics work. (`Class` and
    // `SEL` are intentionally left as plain typedefs: mapping them to builtins
    // breaks the reverse-mapping of the `_cmd`/`self` locals a method binds.)
    if (ast.getLangOpts().ObjC && unqualified_name(td) == "id")
      return ast.getObjCIdType();
    clang::QualType underlying = GenerateType(ts, td->GetUnderlyingType());
    if (!underlying.isNull()) {
      llvm::StringRef name = unqualified_name(td);
      auto *decl = clang::TypedefDecl::Create(
          ast, decl_ctx, clang::SourceLocation(), clang::SourceLocation(),
          &ast.Idents.get(name), ast.getTrivialTypeSourceInfo(underlying));
      decl->setAccess(clang::AS_public);
      decl_ctx->addDecl(decl);
      result = ast.getTypedefType(clang::ElaboratedTypeKeyword::None,
                                  /*Qualifier=*/std::nullopt, decl);
    }
  } else if (auto *cv = llvm::dyn_cast<ct::CVQualifiedType>(cpp_type)) {
    clang::QualType underlying = GenerateType(ts, cv->GetUnderlyingType());
    if (!underlying.isNull()) {
      if (cv->IsConst())
        underlying.addConst();
      if (cv->IsVolatile())
        underlying.addVolatile();
      result = underlying;
    }
  } else if (auto *pa = llvm::dyn_cast<ct::PtrAuthType>(cpp_type)) {
    // A `__ptrauth`-qualified pointer. Generate the underlying (signed) type
    // and re-attach a matching clang PointerAuthQualifier so that clang's
    // codegen authenticates the loaded pointer before dereferencing/calling it
    // (using SignAndAuth, the mode clang applies to a source `__ptrauth`
    // attribute). Without this the sugar would fall through to GenerateBuiltin
    // and collapse to a plain pointer-sized integer, so e.g. calling a
    // `__ptrauth`-signed function pointer would fail with "called object type
    // 'unsigned long long' is not a function or function pointer".
    clang::QualType underlying = GenerateType(ts, pa->GetUnderlyingType());
    if (!underlying.isNull() && !underlying.getPointerAuth()) {
      clang::PointerAuthQualifier qual = clang::PointerAuthQualifier::Create(
          pa->GetKey(), pa->IsAddressDiscriminated(),
          pa->GetExtraDiscriminator(),
          clang::PointerAuthenticationMode::SignAndAuth,
          /*IsIsaPointer=*/false, /*AuthenticatesNullValues=*/false);
      result = ast.getPointerAuthType(underlying, qual);
    } else {
      result = underlying;
    }
  } else if (auto *elab = llvm::dyn_cast<ct::ElaboratedType>(cpp_type)) {
    // ElaboratedType is pure display sugar (a preserved source spelling like
    // `::Struct` or the elaborated `A` a result/persistent type kept). It is
    // fully transparent for codegen, so generate the underlying type. Without
    // this case the sugar falls through to GenerateBuiltin and fails to
    // translate (e.g. a persistent variable `$p` whose stored type is an
    // elaborated record couldn't be referenced).
    result = GenerateType(ts, elab->GetUnderlyingType());
  } else if (auto *en = llvm::dyn_cast<ct::EnumType>(cpp_type)) {
    clang::QualType integer;
    if (ct::Type *ut = en->GetUnderlyingType())
      integer = GenerateType(ts, ut);
    if (integer.isNull())
      integer = ast.IntTy;

    auto *decl = clang::EnumDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    decl->setDeclContext(decl_ctx);
    llvm::StringRef name = unqualified_name(en);
    if (!name.empty())
      decl->setDeclName(&ast.Idents.get(name));
    decl->setScoped(en->IsScoped());
    decl->setScopedUsingClassTag(en->IsScoped());
    decl->setFixed(false);
    decl->setAccess(clang::AS_public);
    decl_ctx->addDecl(decl);
    decl->startDefinition();
    decl->setIntegerType(integer);

    // In C++, an enumerator's type is the enclosing enumeration type (not the
    // underlying integer type). This is what makes `+E::e` apply the enum's
    // integral promotion (via EnumDecl::getPromotionType) rather than leaving
    // the value at the underlying type. Mirror TypeSystemClang here.
    clang::QualType enum_qt = ast.getCanonicalTagType(decl);

    const bool is_signed = en->IsSigned();
    unsigned width = ast.getIntWidth(integer);
    for (const ct::Enumerator &e : en->GetEnumerators()) {
      llvm::APSInt value(llvm::APInt(width, e.value, is_signed), !is_signed);
      auto *ecd = clang::EnumConstantDecl::CreateDeserialized(
          ast, clang::GlobalDeclID());
      ecd->setDeclContext(decl);
      ecd->setDeclName(&ast.Idents.get(e.name.GetName()));
      ecd->setType(enum_qt);
      ecd->setInitVal(ast, value);
      ecd->setAccess(clang::AS_public);
      decl->addDecl(ecd);
    }

    unsigned num_negative = 0, num_positive = 0;
    ast.computeEnumBits(decl->enumerators(), num_negative, num_positive);
    clang::QualType best_type, best_promotion;
    ast.computeBestEnumTypes(/*IsPacked=*/false, num_negative, num_positive,
                             best_type, best_promotion);
    decl->completeDefinition(integer, best_promotion, num_positive,
                             num_negative);
    result = ast.getCanonicalTagType(decl);
  } else if (auto *fn = llvm::dyn_cast<ct::FunctionType>(cpp_type)) {
    clang::QualType ret =
        fn->GetReturnType() ? GenerateType(ts, fn->GetReturnType()) : ast.VoidTy;
    if (ret.isNull())
      ret = ast.VoidTy;
    llvm::SmallVector<clang::QualType, 4> params;
    bool ok = true;
    for (uint32_t i = 0; i < fn->GetNumParameters(); ++i) {
      clang::QualType p = GenerateType(ts, fn->GetParameterAtIndex(i));
      if (p.isNull()) {
        ok = false;
        break;
      }
      params.push_back(p);
    }
    if (ok) {
      clang::FunctionProtoType::ExtProtoInfo epi;
      epi.Variadic = fn->IsVariadic();
      result = ast.getFunctionType(ret, params, epi);
    }
  } else if (auto *cx = llvm::dyn_cast<ct::ComplexType>(cpp_type)) {
    clang::QualType elem = GenerateType(ts, cx->GetElementType());
    if (!elem.isNull())
      result = ast.getComplexType(elem);
  } else {
    result = GenerateBuiltin(cpp_type);
  }

  if (result.isNull()) {
    LLDB_LOG(log, "ClangASTGenerator: failed to translate cpp type '{0}'",
             cpp_type->GetName().GetName());
    return {};
  }

  m_generated[cpp_type] = result.getAsOpaquePtr();
  noteReverse(m_reverse, m_type_owner, ts, result, cpp_type);
  return result;
}

bool ClangASTGenerator::RedirectToCrossModuleDefinition(
    TypeSystemCpp *&ts, ct::RecordType *&rec) {
  if (!m_target || !rec)
    return false;
  // Only records with a name can be looked up in another module; an unnamed /
  // expression-local record can't have a matching definition elsewhere.
  llvm::StringRef name = rec->GetName().GetName();
  if (name.empty())
    return false;

  Log *log = GetLog(LLDBLog::Expressions);

  // Search every module of the target for a type with this fully-qualified
  // name. The complete definition may live in a different module than the one
  // this (forward-declared) record was parsed from (e.g. a type declared in a
  // shared library but defined in the main executable).
  TypeResults results;
  TypeQuery query(name, TypeQueryOptions::e_exact_match);
  m_target->GetImages().FindTypes(nullptr, query, results);

  for (const lldb::TypeSP &type_sp : results.GetTypeMap().Types()) {
    if (!type_sp)
      continue;
    CompilerType candidate = type_sp->GetFullCompilerType();
    auto candidate_ts = candidate.GetTypeSystem<TypeSystemCpp>();
    if (!candidate_ts)
      continue;
    ct::Type *cand = TypeSystemCpp::GetCppType(candidate.GetOpaqueQualType());
    auto *cand_rec = llvm::dyn_cast_or_null<ct::RecordType>(cand);
    if (!cand_rec || cand_rec == rec)
      continue;
    // Ask this candidate's own module to complete it; if it succeeds we have a
    // usable, fully-defined record to read the layout from.
    candidate.GetCompleteType();
    if (!cand_rec->IsComplete())
      continue;
    LLDB_LOG(log,
             "ClangASTGenerator: completing forward-declared record '{0}' "
             "from a cross-module definition",
             name);
    // Remember the complete definition (carrying its own module's TypeSystem)
    // so a result type that maps back to the incomplete record can be sized
    // from it (see MapClangTypeToCpp).
    m_cross_module_complete[rec] = candidate;
    ts = candidate_ts.get();
    rec = cand_rec;
    return true;
  }
  return false;
}

bool ClangASTGenerator::RedirectObjCInterfaceToRuntimeDefinition(
    TypeSystemCpp *&ts, ct::ObjCInterfaceType *&iface) {
  if (!m_target || !iface)
    return false;
  lldb::ProcessSP process_sp = m_target->GetProcessSP();
  if (!process_sp)
    return false;
  ObjCLanguageRuntime *runtime = ObjCLanguageRuntime::Get(*process_sp);
  if (!runtime)
    return false;

  // Ask TypeSystemCpp's own runtime-reconstruction directly (as the
  // frame-variable/DIL path does via GetRuntimeCompletedObjCType) rather than
  // going through ObjCLanguageRuntime::GetRuntimeType()/LookupInRuntime(): the
  // latter is a ValueObject-facing cache keyed off DeclVendor::FindDecls(),
  // which CppObjCDeclVendor deliberately never answers (see
  // CppObjCDeclVendor.h) because a runtime-reconstructed CompilerType handed
  // back there gets used to override a *live value's* displayed type -- wrong
  // for ivars the runtime can only type crudely (id/Class/SEL become opaque
  // pointers). Here the reconstructed type only ever backs a throwaway
  // per-expression synthesized clang AST, so that hazard doesn't apply, and
  // going direct also means this doesn't depend on which DeclVendor happens
  // to be installed.
  auto scratch_or =
      m_target->GetScratchTypeSystemForLanguage(lldb::eLanguageTypeObjC_plus_plus);
  if (!scratch_or) {
    llvm::consumeError(scratch_or.takeError());
    return false;
  }
  auto *scratch = llvm::dyn_cast_or_null<TypeSystemCpp>(scratch_or->get());
  if (!scratch)
    return false;
  ConstString class_name(iface->GetName().GetName());
  if (!class_name)
    return false;
  CompilerType runtime_ct =
      scratch->CreateRuntimeObjCInterface(class_name, *process_sp, *runtime);
  if (!runtime_ct)
    return false;
  auto *runtime_iface = llvm::dyn_cast_or_null<ct::ObjCInterfaceType>(
      TypeSystemCpp::GetCppType(runtime_ct.GetOpaqueQualType()));
  if (!runtime_iface || runtime_iface == iface)
    return false;
  // Only redirect when the runtime's answer actually has more to offer than
  // what debug info already gave us -- either more ivars (e.g. a class
  // extension implemented in a different image) or, since a class like
  // NSString may have plenty of debug info but zero *methods* in it (its
  // methods are implemented in Foundation, without debug info), more
  // methods; otherwise keep answering directly from (potentially
  // better-typed) debug info. CompleteMemberFunctions() first so the
  // comparison isn't against an as-yet-unparsed (lazy) method list.
  ts->CompleteMemberFunctions(iface);
  if (runtime_iface->GetNumFields() <= iface->GetNumFields() &&
      runtime_iface->GetNumObjCMethods() <= iface->GetNumObjCMethods())
    return false;

  LLDB_LOG(GetLog(LLDBLog::Expressions),
           "ClangASTGenerator: completing Objective-C interface '{0}' from "
           "the ObjC runtime (ivars/methods hidden from this module's debug "
           "info)",
           iface->GetName().GetName());
  ts = scratch;
  iface = runtime_iface;
  return true;
}

cpp_typesystem::ObjCInterfaceType *
ClangASTGenerator::GetModuleObjCInterface(llvm::StringRef class_name,
                                          TypeSystemCpp *&ts) {
  if (!m_target || class_name.empty())
    return nullptr;

  // Only consult the module vendor if one already exists (some `@import`
  // brought a module in this session); never lazily create it here.
  auto *persistent = llvm::dyn_cast_or_null<ClangPersistentVariables>(
      m_target->GetPersistentExpressionStateForLanguage(lldb::eLanguageTypeC));
  if (!persistent)
    return nullptr;
  std::shared_ptr<ClangModulesDeclVendor> vendor =
      persistent->GetExistingClangModulesDeclVendor();
  if (!vendor)
    return nullptr;

  std::vector<CompilerDecl> found;
  if (!vendor->FindDecls(ConstString(class_name), /*append=*/false,
                         /*max_matches=*/UINT32_MAX, found))
    return nullptr;

  const clang::ObjCInterfaceDecl *iface_decl = nullptr;
  clang::ASTContext *vendor_ast = nullptr;
  for (const CompilerDecl &cd : found) {
    if (auto *d = llvm::dyn_cast_or_null<clang::ObjCInterfaceDecl>(
            static_cast<clang::Decl *>(cd.GetOpaqueDecl()))) {
      iface_decl = d;
      vendor_ast = &d->getASTContext();
      break;
    }
  }
  if (!iface_decl || !iface_decl->getDefinition())
    return nullptr;

  auto scratch_or = m_target->GetScratchTypeSystemForLanguage(
      lldb::eLanguageTypeObjC_plus_plus);
  if (!scratch_or) {
    llvm::consumeError(scratch_or.takeError());
    return nullptr;
  }
  auto *scratch = llvm::dyn_cast_or_null<TypeSystemCpp>(scratch_or->get());
  if (!scratch)
    return nullptr;

  // Translate the module's clang interface (methods with typedef'd signatures,
  // ivars, superclass) into a cpp interface owned by the scratch TypeSystemCpp.
  ClangTypeConverter converter(*this, *scratch, vendor_ast);
  CompilerType iface_ct =
      converter.ConvertObjCInterface(iface_decl, /*complete=*/true);
  auto *iface = llvm::dyn_cast_or_null<ct::ObjCInterfaceType>(
      TypeSystemCpp::GetCppType(iface_ct.GetOpaqueQualType()));
  if (!iface)
    return nullptr;
  ts = scratch;
  return iface;
}

void ClangASTGenerator::PopulateRecord(clang::RecordDecl *record_decl) {
  auto it = m_records.find(record_decl);
  if (it == m_records.end())
    return;
  RecordInfo &info = *it->second;
  if (info.completed)
    return;
  info.completed = true;

  TypeSystemCpp &ts_ref = *info.ts;
  TypeSystemCpp *ts = &ts_ref;
  ct::RecordType *rec = info.cpp_record;
  clang::CXXRecordDecl *decl = info.clang_decl;
  clang::ASTContext &ast = m_ast;

  // Make sure the record's members are parsed from debug info before we read
  // them out.
  CompilerType cpp_ct = ts->GetCompilerType(rec);
  cpp_ct.GetCompleteType();

  // The record may only be forward-declared in the module it was parsed from
  // (its complete definition living in another module of the target). We are on
  // the by-value completion path -- Clang genuinely needs this record's size
  // and members here -- so it is safe to pull in the complete definition from
  // wherever it lives. Reading the layout from the other module's (complete)
  // record leaves this record's clang decl / reverse mapping untouched.
  if (!rec->IsComplete())
    RedirectToCrossModuleDefinition(ts, rec);

  // The record was only ever forward-declared in the debug info (no definition
  // exists in any module). Leave the clang decl as an incomplete forward
  // declaration -- do NOT startDefinition()/completeDefinition() it, or Clang
  // would treat it as a complete empty struct and, e.g., diagnose `fwd->i` as
  // "no member named 'i'" instead of the correct "member access into incomplete
  // type 'Forward'". Clear the external-storage flags so Clang does not keep
  // asking us to complete a record we cannot complete.
  if (!rec->IsComplete()) {
    decl->setHasExternalLexicalStorage(false);
    decl->setHasExternalVisibleStorage(false);
    return;
  }

  // Record which cpp record the layout is actually derived from, so
  // LayoutRecord reports a size/base offsets consistent with the fields added
  // below (they may come from a cross-module definition).
  info.layout_record = rec;

  decl->startDefinition();

  // Base classes (C++ classes only). A base subobject is embedded by value, so
  // Clang requires its full definition when finalizing this record; complete
  // each base before wiring it up.
  if (rec->GetNumBaseClasses()) {
    std::vector<clang::CXXBaseSpecifier *> bases;
    for (uint32_t i = 0; i < rec->GetNumBaseClasses(); ++i) {
      const ct::BaseClass *base = rec->GetBaseClassAtIndex(i);
      // A base only needs completing for layout (EnsureComplete below), not a
      // template specialization decl -- build it lazily.
      clang::QualType base_qt =
          GenerateType(*ts, base->type.Get(), /*build_template_spec=*/false);
      if (base_qt.isNull())
        continue;
      EnsureComplete(base_qt);
      bases.push_back(new (ast) clang::CXXBaseSpecifier(
          clang::SourceRange(), /*is_virtual=*/base->is_virtual,
          /*base_of_class=*/true, clang::AS_public,
          ast.getTrivialTypeSourceInfo(base_qt), clang::SourceLocation()));
    }
    if (!bases.empty())
      decl->setBases(bases.data(), bases.size());
  }

  // Fields. DWARF does not emit unnamed/zero-width padding bitfields, so two
  // named bitfields that share a storage unit can end up non-contiguous (a gap
  // where the padding used to be). Clang's record-layout codegen requires the
  // bitfields in a run to be bit-contiguous, so we synthesize an unnamed
  // bitfield to fill such a gap. This is only needed when a bitfield starts
  // mid-byte right after another bitfield: a byte-aligned bitfield begins a
  // fresh access unit (no contiguity requirement), and a bitfield never starts
  // mid-byte after a non-bitfield. Restricting to exactly this case avoids
  // inserting spurious padding that would otherwise overlap in the lowering.
  bool prev_is_bitfield = false;
  uint64_t prev_bitfield_end = 0; // Bit offset just past the previous bitfield.

  auto abs_bit_offset = [](const ct::Field *f) -> uint64_t {
    return f->byte_offset * 8 + f->bitfield_bit_offset;
  };

  // Unnamed struct/union members, collected so their members can be promoted
  // (C11 anonymous struct/union) after the field loop.
  llvm::SmallVector<clang::FieldDecl *, 4> anon_fields;

  for (uint32_t i = 0; i < rec->GetNumFields(); ++i) {
    const ct::Field *field = rec->GetFieldAtIndex(i);
    // A by-value field is completed for layout (EnsureComplete below) but does
    // not need a template specialization decl of its own here; build it lazily
    // so a template-typed field doesn't drag its whole specialization graph in.
    clang::QualType field_qt =
        GenerateType(*ts, field->type.Get(), /*build_template_spec=*/false);
    if (field_qt.isNull())
      continue;
    // A field held by value (directly or as an array element) is embedded in
    // this record, so it must be complete before we finalize the definition.
    EnsureComplete(field_qt);

    const uint64_t this_offset = abs_bit_offset(field);

    // Fill the gap left by an omitted padding bitfield between two bitfields in
    // the same storage unit (see the comment above).
    if (field->IsBitfield() && prev_is_bitfield && (this_offset % 8) != 0 &&
        this_offset > prev_bitfield_end) {
      uint64_t unnamed_bit_size = this_offset - prev_bitfield_end;
      auto *pad =
          clang::FieldDecl::CreateDeserialized(ast, clang::GlobalDeclID());
      pad->setDeclContext(decl);
      pad->setType(ast.IntTy);
      llvm::APInt pad_width(ast.getIntWidth(ast.IntTy), unnamed_bit_size);
      auto *pad_literal = clang::IntegerLiteral::Create(
          ast, pad_width, ast.IntTy, clang::SourceLocation());
      pad->setBitWidth(clang::ConstantExpr::Create(
          ast, pad_literal, clang::APValue(llvm::APSInt(pad_width))));
      pad->setAccess(clang::AS_public);
      decl->addDecl(pad);
      info.field_bit_offsets[pad] = prev_bitfield_end;
    }

    clang::Expr *bit_width = nullptr;
    if (field->IsBitfield()) {
      llvm::APInt width(ast.getIntWidth(ast.IntTy), field->bitfield_bit_size);
      clang::Expr *literal = clang::IntegerLiteral::Create(
          ast, width, ast.IntTy, clang::SourceLocation());
      bit_width = clang::ConstantExpr::Create(ast, literal,
                                              clang::APValue(llvm::APSInt(width)));
    }

    auto *fd = clang::FieldDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    fd->setDeclContext(decl);
    if (!field->name.GetName().empty())
      fd->setDeclName(&ast.Idents.get(field->name.GetName()));
    fd->setType(field_qt);
    if (bit_width)
      fd->setBitWidth(bit_width);
    fd->setAccess(clang::AS_public);
    decl->addDecl(fd);
    info.field_bit_offsets[fd] = this_offset;

    // A field with no name whose type is a struct/union is a C11 anonymous
    // struct/union member; remember it so we can promote its members below.
    if (field->name.GetName().empty() && field_qt->isRecordType())
      anon_fields.push_back(fd);

    prev_is_bitfield = field->IsBitfield();
    if (field->IsBitfield())
      prev_bitfield_end = this_offset + field->bitfield_bit_size;
  }

  // C11 anonymous struct/union member promotion: make the members of each
  // unnamed struct/union member reachable directly on this record via
  // IndirectFieldDecls, so clang name lookup resolves e.g. `n->foo` / `n->b`.
  // (The frame-variable path handles this separately via
  // TypeSystemCpp::GetIndexOfChildMemberWithName.) Nested anonymous members
  // already have their own IndirectFieldDecls -- built when EnsureComplete
  // populated them above -- so extending those chains handles multiple levels.
  for (clang::FieldDecl *anon : anon_fields) {
    const auto *rt = anon->getType()->getAs<clang::RecordType>();
    if (!rt)
      continue;
    clang::RecordDecl *nested = rt->getDecl()->getDefinition();
    if (!nested)
      continue;
    for (clang::Decl *d : nested->decls()) {
      llvm::SmallVector<clang::NamedDecl *, 4> chain;
      chain.push_back(anon);
      clang::DeclarationName member_name;
      clang::QualType member_ty;
      if (auto *nf = llvm::dyn_cast<clang::FieldDecl>(d)) {
        // Skip unnamed nested fields (e.g. a nested anonymous member or
        // padding); a nested anonymous member's own members are promoted via
        // its IndirectFieldDecls, handled below.
        if (!nf->getDeclName())
          continue;
        chain.push_back(nf);
        member_name = nf->getDeclName();
        member_ty = nf->getType();
      } else if (auto *nifd = llvm::dyn_cast<clang::IndirectFieldDecl>(d)) {
        for (clang::NamedDecl *link : nifd->chain())
          chain.push_back(link);
        member_name = nifd->getDeclName();
        member_ty = nifd->getType();
      } else {
        continue;
      }
      auto **chain_arr = new (ast) clang::NamedDecl *[chain.size()];
      for (size_t i = 0; i < chain.size(); ++i)
        chain_arr[i] = chain[i];
      auto *ifd = clang::IndirectFieldDecl::Create(
          ast, decl, clang::SourceLocation(), member_name.getAsIdentifierInfo(),
          member_ty, {chain_arr, chain.size()});
      ifd->setAccess(clang::AS_public);
      ifd->setImplicit();
      decl->addDecl(ifd);
    }
  }

  // Member functions. Declaring them (with the asm label the JIT resolves)
  // lets an expression call `obj.method()` / `this->method()`. They are parsed
  // lazily -- separately from the record's fields/bases -- so make sure they've
  // been filled in now that we're building the clang decl.
  ts->CompleteMemberFunctions(rec);
  for (uint32_t i = 0; i < rec->GetNumMemberFunctions(); ++i) {
    const ct::MemberFunction *mf = rec->GetMemberFunctionAtIndex(i);
    clang::QualType method_qt = GenerateType(*ts, mf->type.Get());
    if (method_qt.isNull())
      continue;
    // The cpp_typesystem FunctionType doesn't carry the method's cv-qualifiers
    // (the `const`/`volatile` in `int func() const`) or ref-qualifier (the
    // `&`/`&&` in `int func() &`), so the type produced above is the plain,
    // unqualified signature. Rebuild it applying the method qualifiers,
    // otherwise a `const`/`volatile`/non-`const` (or `&`/`&&`) overload set
    // collapses into identical methods (ambiguous calls) and calling a
    // non-const method on a const object is wrongly accepted.
    bool has_ref_qualifier =
        mf->ref_qualifier != ct::RefQualifier::None;
    if (!mf->is_static &&
        (mf->is_const || mf->is_volatile || has_ref_qualifier)) {
      if (const auto *proto = method_qt->getAs<clang::FunctionProtoType>()) {
        clang::FunctionProtoType::ExtProtoInfo epi = proto->getExtProtoInfo();
        if (mf->is_const || mf->is_volatile) {
          clang::Qualifiers quals = epi.TypeQuals;
          if (mf->is_const)
            quals.addConst();
          if (mf->is_volatile)
            quals.addVolatile();
          epi.TypeQuals = quals;
        }
        switch (mf->ref_qualifier) {
        case ct::RefQualifier::None:
          break;
        case ct::RefQualifier::LValue:
          epi.RefQualifier = clang::RQ_LValue;
          break;
        case ct::RefQualifier::RValue:
          epi.RefQualifier = clang::RQ_RValue;
          break;
        }
        method_qt = ast.getFunctionType(proto->getReturnType(),
                                        proto->getParamTypes(), epi);
      }
    }
    // Constructors and destructors must be built as CXXConstructorDecl /
    // CXXDestructorDecl with the proper C++ declaration name, otherwise clang
    // won't recognize a functional-style cast like `Foo(2)` (or `new Foo(2)`)
    // as a constructor call. Ordinary methods are plain CXXMethodDecls named by
    // their identifier.
    clang::CXXMethodDecl *method = nullptr;
    switch (mf->kind) {
    case ct::MemberFunctionKind::Constructor: {
      auto *ctor = clang::CXXConstructorDecl::CreateDeserialized(
          ast, clang::GlobalDeclID(), /*NumCtorInitializers=*/0);
      ctor->setDeclName(ast.DeclarationNames.getCXXConstructorName(
          ast.getCanonicalType(ast.getCanonicalTagType(decl))));
      ctor->setNumCtorInitializers(0);
      ctor->setExplicitSpecifier(clang::ExplicitSpecifier(
          nullptr, clang::ExplicitSpecKind::ResolvedFalse));
      method = ctor;
      break;
    }
    case ct::MemberFunctionKind::Destructor: {
      auto *dtor = clang::CXXDestructorDecl::CreateDeserialized(
          ast, clang::GlobalDeclID());
      dtor->setDeclName(ast.DeclarationNames.getCXXDestructorName(
          ast.getCanonicalType(ast.getCanonicalTagType(decl))));
      method = dtor;
      break;
    }
    case ct::MemberFunctionKind::Method: {
      // An overloaded operator (`operator==`, `operator->`, `operator[]`, ...)
      // or a conversion operator (`operator int`) must be built with the proper
      // C++ declaration name (and, for a conversion, as a CXXConversionDecl),
      // otherwise clang won't resolve operator syntax (`a == b`, `*a`, `a->m`,
      // `a[i]`) or a cast (`static_cast<int>(a)`) to it -- a plain identifier
      // decl name would only match a call spelled with the literal `operator...`
      // name. A method whose name merely starts with "operator" but isn't one
      // of these (e.g. `operatorint`) stays a plain identifier.
      llvm::StringRef mf_name = mf->name.GetName();
      clang::OverloadedOperatorKind oo = clang::OO_None;
      bool is_conversion = false;
      {
        llvm::StringRef rest = mf_name;
        if (rest.consume_front("operator")) {
          // clang spells single-token operators without a space
          // (`operator==`), but `new`/`delete` and conversion operators with
          // one (`operator new`, `operator int`).
          llvm::StringRef token = rest.ltrim();
          bool had_space = rest.size() != token.size();
          oo = GetOverloadedOperatorKind(token);
          // An explicitly-instantiated templated operator (e.g.
          // `unique_ptr<int>::operator*<int *, 0>()`) has its explicit
          // template argument list appended directly after the operator
          // token with no separator in the DWARF spelling (`operator*<int
          // *, 0>`), so the exact match above fails. Strip the trailing
          // `<...>` and retry so `*s`/`v[i]`-style syntax still binds to it.
          if (oo == clang::OO_None) {
            if (size_t lt = token.find('<');
                lt != llvm::StringRef::npos && lt != 0)
              oo = GetOverloadedOperatorKind(token.substr(0, lt));
          }
          // A word-spelled operator (`operator new`, `operator co_await`) is
          // only an operator when spelled with a space; without one it is an
          // ordinary method whose name happens to start with "operator" (e.g.
          // `operatornew`).
          bool token_is_word = !token.empty() && llvm::isAlpha(token.front());
          if (oo != clang::OO_None && token_is_word && !had_space)
            oo = clang::OO_None;
          // A conversion operator: the name after `operator` is a type spelling
          // (`int`, `long`, ...) rather than an operator token.
          if (oo == clang::OO_None && had_space && !token.empty())
            is_conversion = true;
        }
      }

      if (is_conversion) {
        auto *conv = clang::CXXConversionDecl::CreateDeserialized(
            ast, clang::GlobalDeclID());
        const auto *proto = method_qt->getAs<clang::FunctionProtoType>();
        clang::QualType conv_ty =
            proto ? proto->getReturnType() : clang::QualType();
        conv->setDeclName(ast.DeclarationNames.getCXXConversionFunctionName(
            ast.getCanonicalType(conv_ty)));
        conv->setExplicitSpecifier(clang::ExplicitSpecifier(
            nullptr, clang::ExplicitSpecKind::ResolvedFalse));
        method = conv;
      } else {
        method =
            clang::CXXMethodDecl::CreateDeserialized(ast, clang::GlobalDeclID());
        if (oo != clang::OO_None)
          method->setDeclName(ast.DeclarationNames.getCXXOperatorName(oo));
        else
          method->setDeclName(&ast.Idents.get(mf->name.GetName()));
      }
      break;
    }
    }
    method->setDeclContext(decl);
    method->setType(method_qt);
    method->setStorageClass(mf->is_static ? clang::SC_Static : clang::SC_None);
    method->setConstexprKind(clang::ConstexprSpecKind::Unspecified);
    method->setAccess(clang::AS_public);
    method->setVirtualAsWritten(mf->is_virtual);
    // A virtual method with a covariant return type (an override returning a
    // pointer/reference to a more-derived class than the base method) makes
    // clang compute a covariant-return thunk when it lays out the vtable, which
    // queries the layout of the returned class. Complete that record now so the
    // query doesn't hit a forward declaration (which asserts in clang).
    if (mf->is_virtual) {
      if (const auto *proto = method_qt->getAs<clang::FunctionProtoType>()) {
        clang::QualType ret = proto->getReturnType();
        if (ret->isPointerType() || ret->isReferenceType())
          ret = ret->getPointeeType();
        EnsureComplete(ret);
      }
    }
    if (!mf->asm_label.GetName().empty())
      method->addAttr(
          clang::AsmLabelAttr::CreateImplicit(ast, mf->asm_label.GetName()));
    BuildParams(method, method_qt);
    decl->addDecl(method);
  }

  // Static data members. Declaring them as static VarDecls inside the record
  // lets an expression name `record.s` / `Record::s`. A constant integral
  // member (`static const`/`constexpr`) gets an initializer so it folds to its
  // value; a member with storage is resolved at runtime via its mangled name
  // (clang reconstructs that name from the record's qualified name, or from the
  // asm label when the declaration carried one).
  for (uint32_t i = 0; i < rec->GetNumStaticDataMembers(); ++i) {
    const ct::StaticDataMember *sm = rec->GetStaticDataMemberAtIndex(i);
    clang::QualType member_qt = GenerateType(*ts, sm->type.Get());
    if (member_qt.isNull())
      continue;

    auto *vd = clang::VarDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    vd->setDeclContext(decl);
    if (!sm->name.GetName().empty())
      vd->setDeclName(&ast.Idents.get(sm->name.GetName()));
    vd->setType(member_qt);
    vd->setStorageClass(clang::SC_Static);
    vd->setAccess(clang::AS_public);
    if (!sm->mangled_name.GetName().empty())
      vd->addAttr(
          clang::AsmLabelAttr::CreateImplicit(ast, sm->mangled_name.GetName()));

    // For a constant integral member, attach an initializer so `Record::c`
    // folds to a compile-time constant (usable without a running target).
    if (sm->HasConstValue() && member_qt->isIntegralOrEnumerationType()) {
      clang::QualType init_qt = member_qt;
      if (const auto *et = init_qt->getAs<clang::EnumType>())
        init_qt = et->getDecl()->getDefinitionOrSelf()->getIntegerType();
      unsigned width = ast.getIntWidth(init_qt);
      bool is_signed = init_qt->isSignedIntegerOrEnumerationType();
      llvm::APInt value(width, *sm->const_value, is_signed);
      if (init_qt->isSpecificBuiltinType(clang::BuiltinType::Bool))
        vd->setInit(clang::CXXBoolLiteralExpr::Create(
            ast, !value.isZero(), init_qt.getUnqualifiedType(),
            clang::SourceLocation()));
      else
        vd->setInit(clang::IntegerLiteral::Create(
            ast, value, init_qt.getUnqualifiedType(), clang::SourceLocation()));
      vd->setConstexpr(true);
    }

    decl->addDecl(vd);
  }

  if (!decl->isCompleteDefinition())
    decl->completeDefinition();

  // Reproduce the record's by-value argument-passing ABI from DWARF's
  // DW_AT_calling_convention (see DWARFASTParserCpp). Without this, a struct
  // that must be passed by reference / returned via sret (e.g. one with a
  // non-trivial copy constructor) would be miscompiled when an expression
  // calls a function taking/returning it by value, corrupting the result
  // pointer. Mirrors DWARFASTParserClang's setArgPassingRestrictions /
  // setHasTrivialSpecialMemberForCall.
  if (auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(decl)) {
    switch (rec->GetArgPassingKind()) {
    case ct::RecordType::ArgPassingKind::PassByValue:
      cxx->setHasTrivialSpecialMemberForCall();
      break;
    case ct::RecordType::ArgPassingKind::CannotPassInRegs:
      cxx->setArgPassingRestrictions(
          clang::RecordArgPassingKind::CannotPassInRegs);
      break;
    case ct::RecordType::ArgPassingKind::Unspecified:
      break;
    }
  }

  // Clear the external-storage flags before iterating the members below: the
  // members were added directly (addDecl), so no external source is needed to
  // enumerate them, and leaving the flags set would make `decl->methods()`
  // try to load from an external source that a sourceless context (e.g. the
  // `target modules dump ast` path) does not have.
  decl->setHasLoadedFieldsFromExternalStorage(true);
  decl->setHasExternalLexicalStorage(false);
  // Keep visible (name-lookup) storage on when the record declares nested
  // types, so a qualified reference like `Record::Nested` calls back through
  // the external source (CppExpressionDeclMap::FindExternalVisibleDecls, which
  // resolves it via LookupNestedType). Nested types are resolved lazily rather
  // than emitted here to avoid driving completion into infinite recursion for a
  // self-referential nested type (common in libc++ containers). A record with
  // no nested types needs no such callback.
  decl->setHasExternalVisibleStorage(rec->GetNumNestedTypes() != 0);

  // Wire up virtual-method overrides now that the class (and its bases) are
  // complete. Clang derives the vtable layout from these override links; an
  // overriding method must share its base method's slot, otherwise a virtual
  // call dispatches through a wrong/out-of-range slot and crashes.
  for (clang::CXXMethodDecl *method : decl->methods())
    AddOverridesForMethod(method);

  if (auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(decl))
    MarkImplicitCopyOpsDeletedByUserMove(cxx);
}

void ClangASTGenerator::MarkImplicitCopyOpsDeletedByUserMove(
    clang::CXXRecordDecl *decl) {
  // Real Sema eagerly declares (and, if needed, deletes) a class's implicit
  // copy constructor/assignment right when the class body closes, whenever
  // needsOverloadResolutionFor{CopyConstructor,CopyAssignment}() is already
  // knowable at that point (Sema::AddImplicitlyDeclaredMembersToClass). We
  // build the whole class in one shot via addDecl() (no incremental Sema), so
  // that eager declaration never runs; if a *different*, later-completed
  // record embeds this class as a field/base, CXXRecordDecl::addedClassSubobject
  // calls hasSimpleCopyConstructor(), which asserts that this has already been
  // decided (DeclCXX.h's "this property has not yet been computed by Sema").
  //
  // Fully replicating Sema::ShouldDeleteSpecialMember would require running
  // overload resolution over base/member subobjects, which needs a live Sema
  // we don't have here (ClangASTGenerator is also used, Sema-less, to build a
  // throwaway AST for `target modules dump ast`). Instead, replicate just the
  // cheap, purely-syntactic rule that covers the common case (and this test):
  // C++11 [class.copy]p7,p18/[class.copy.assign]p2 -- a class that declares a
  // move constructor or move assignment operator implicitly deletes its copy
  // constructor/assignment operator, no overload resolution required. A class
  // whose "needs overload resolution" bit is set for a different reason (a
  // subobject that itself needs overload resolution) is left alone; something
  // that later forces Sema to actually declare the copy ctor/assignment will
  // compute it lazily as usual.
  if (decl->needsImplicitCopyConstructor() &&
      decl->needsOverloadResolutionForCopyConstructor() &&
      decl->hasUserDeclaredMoveOperation())
    decl->setImplicitCopyConstructorIsDeleted();
  if (decl->needsImplicitCopyAssignment() &&
      decl->needsOverloadResolutionForCopyAssignment() &&
      decl->hasUserDeclaredMoveOperation())
    decl->setImplicitCopyAssignmentIsDeleted();
}

void ClangASTGenerator::BuildParams(clang::FunctionDecl *func,
                                    clang::QualType function_qt) {
  const auto *proto = function_qt->getAs<clang::FunctionProtoType>();
  if (!proto)
    return;
  llvm::SmallVector<clang::ParmVarDecl *, 4> params;
  for (unsigned i = 0; i < proto->getNumParams(); ++i) {
    auto *param =
        clang::ParmVarDecl::CreateDeserialized(m_ast, clang::GlobalDeclID());
    param->setDeclContext(func);
    param->setType(proto->getParamType(i));
    param->setStorageClass(clang::SC_None);
    params.push_back(param);
  }
  func->setParams(params);
}

clang::FunctionDecl *
ClangASTGenerator::GenerateFunction(llvm::StringRef name,
                                    const CompilerType &function_cpp_type,
                                    llvm::StringRef asm_label,
                                    bool is_extern_c) {
  GenerationGuard guard(*this);
  clang::QualType function_qt = Generate(function_cpp_type);
  if (function_qt.isNull() || !function_qt->getAs<clang::FunctionProtoType>())
    return nullptr;
  clang::ASTContext &ast = m_ast;
  return BuildFunction(clang::DeclarationName(&ast.Idents.get(name)),
                       function_qt, asm_label, is_extern_c);
}

clang::FunctionDecl *
ClangASTGenerator::GenerateFunction(clang::DeclarationName name,
                                    const CompilerType &function_cpp_type,
                                    llvm::StringRef asm_label) {
  GenerationGuard guard(*this);
  clang::QualType function_qt = Generate(function_cpp_type);
  if (function_qt.isNull() || !function_qt->getAs<clang::FunctionProtoType>())
    return nullptr;
  return BuildFunction(name, function_qt, asm_label);
}

clang::FunctionDecl *
ClangASTGenerator::GenerateGenericFunction(llvm::StringRef name) {
  // Held while we synthesize (and add) the decl: adding a named decl to the
  // external-visible TU makes clang look that same name up again, which without
  // this guard would re-enter LookupSymbolFunction -> GenerateGenericFunction
  // and recurse forever (IsGeneratingDecls() gates that re-entrant lookup off).
  GenerationGuard guard(*this);
  clang::ASTContext &ast = m_ast;
  // A variadic function returning `__unknown_anytype`, mirroring
  // NameSearchContext::AddGenericFunDecl for symbol-only callees. The
  // expression must cast the result to a concrete type; the JIT binds the
  // callee to the symbol's materialized address (no asm label).
  clang::FunctionProtoType::ExtProtoInfo epi;
  epi.Variadic = true;
  clang::QualType func_qt =
      ast.getFunctionType(ast.UnknownAnyTy, llvm::ArrayRef<clang::QualType>(),
                          epi);
  return BuildFunction(clang::DeclarationName(&ast.Idents.get(name)), func_qt,
                       /*asm_label=*/{});
}

clang::DeclContext *ClangASTGenerator::GetOrCreateExternCContext() {
  clang::ASTContext &ast = m_ast;
  if (!m_extern_c_decl) {
    m_extern_c_decl = clang::LinkageSpecDecl::Create(
        ast, ast.getTranslationUnitDecl(), clang::SourceLocation(),
        clang::SourceLocation(), clang::LinkageSpecLanguageIDs::C,
        /*HasBraces=*/true);
    ast.getTranslationUnitDecl()->addDecl(m_extern_c_decl);
  }
  return m_extern_c_decl;
}

clang::FunctionDecl *
ClangASTGenerator::BuildFunction(clang::DeclarationName name,
                                 clang::QualType function_qt,
                                 llvm::StringRef asm_label, bool is_extern_c) {
  clang::ASTContext &ast = m_ast;
  clang::DeclContext *dc = is_extern_c ? GetOrCreateExternCContext()
                                       : ast.getTranslationUnitDecl();
  auto *fd = clang::FunctionDecl::CreateDeserialized(ast, clang::GlobalDeclID());
  fd->setDeclContext(dc);
  fd->setDeclName(name);
  fd->setType(function_qt);
  fd->setStorageClass(clang::SC_Extern);
  fd->setConstexprKind(clang::ConstexprSpecKind::Unspecified);
  if (!asm_label.empty())
    fd->addAttr(clang::AsmLabelAttr::CreateImplicit(ast, asm_label));
  BuildParams(fd, function_qt);
  dc->addDecl(fd);
  return fd;
}

void ClangASTGenerator::EnsureComplete(clang::QualType qt) {
  if (qt.isNull())
    return;
  // Peel array element types (embedded by value); pointers/references stop the
  // recursion because they only need a forward declaration.
  const clang::Type *type = qt.getCanonicalType().getTypePtr();
  while (const clang::ArrayType *at = m_ast.getAsArrayType(clang::QualType(type, 0)))
    type = at->getElementType().getCanonicalType().getTypePtr();
  if (auto *rd = type->getAsCXXRecordDecl()) {
    CompleteRecord(rd);
    // A record embedded by value (a base subobject or a member) must be a
    // complete clang type: clang's record layout (EmptySubobjectMap, pointer
    // alignment) queries getASTRecordLayout on it and asserts on a forward
    // declaration. CompleteRecord leaves it forward-declared when no definition
    // exists in any module (e.g. a base/field of a type built with
    // -flimit-debug-info whose key function lives in a stripped TU). Forcefully
    // complete it to an empty definition so codegen can lay out the enclosing
    // record, mirroring DWARFASTParserClang's SetDeclIsForcefullyCompleted for
    // the same limited-debug-info case.
    //
    // Only do this when the *cpp* record itself is genuinely incomplete. If the
    // cpp record is complete but the clang decl is not yet a complete definition,
    // we are re-entering while that record is still mid-population (e.g. a class
    // with a member function whose signature mentions the class through a
    // pointer re-enters EnsureComplete on itself before PopulateRecord reaches
    // its completeDefinition()). Forcefully completing it here would wipe the
    // fields/bases the in-progress PopulateRecord is about to add (turning it
    // into an empty struct and losing its members); leave it and let the
    // outstanding PopulateRecord finish the definition.
    auto info_it = m_records.find(rd);
    if (info_it != m_records.end() && !rd->isCompleteDefinition() &&
        info_it->second->cpp_record &&
        !info_it->second->cpp_record->IsComplete()) {
      auto *mrd = const_cast<clang::CXXRecordDecl *>(rd);
      mrd->startDefinition();
      mrd->completeDefinition();
      mrd->setHasExternalLexicalStorage(false);
      mrd->setHasExternalVisibleStorage(false);
    }
  }
}

bool ClangASTGenerator::CompleteRecord(clang::TagDecl *tag_decl) {
  GenerationGuard guard(*this);
  auto *record_decl = llvm::dyn_cast<clang::RecordDecl>(tag_decl);
  if (!record_decl)
    return false;
  if (m_records.find(record_decl) == m_records.end())
    return false;
  PopulateRecord(record_decl);
  return true;
}

void ClangASTGenerator::PopulateObjCInterface(
    clang::ObjCInterfaceDecl *iface_decl) {
  auto it = m_objc_interfaces.find(iface_decl);
  if (it == m_objc_interfaces.end())
    return;
  ObjCInterfaceInfo &info = *it->second;
  if (info.completed)
    return;
  info.completed = true;

  TypeSystemCpp *ts = info.ts;
  ct::ObjCInterfaceType *iface = info.cpp_iface;
  clang::ASTContext &ast = m_ast;

  // Make sure the interface's ivars/superclass are parsed from debug info.
  CompilerType cpp_ct = ts->GetCompilerType(iface);
  cpp_ct.GetCompleteType();

  // Some of this class's ivars may only be visible in the debug info of a
  // different image/module (e.g. a class extension implemented in a shared
  // library, with the main executable only seeing a stub) -- no same-module
  // or debug-map DWARF search finds those. Ask the ObjC runtime, which knows
  // the class's full ivar layout regardless of which image defined it.
  if (RedirectObjCInterfaceToRuntimeDefinition(ts, iface))
    info.cpp_iface = iface;

  // Turn the forward declaration into a definition. Clang lays out the ivars
  // itself using the Objective-C runtime ABI (non-fragile), so -- unlike C++
  // records -- we do not supply field offsets via layoutRecordType.
  iface_decl->startDefinition();

  // Superclass (modeled as the interface's single base class).
  if (const ct::BaseClass *super = iface->GetBaseClassAtIndex(0)) {
    clang::QualType super_qt = GenerateType(*ts, super->type.Get());
    if (!super_qt.isNull() && super_qt->isObjCObjectType()) {
      if (auto *super_obj = super_qt->getAs<clang::ObjCObjectType>()) {
        if (clang::ObjCInterfaceDecl *super_decl = super_obj->getInterface()) {
          CompleteObjCInterface(super_decl);
          iface_decl->setSuperClass(
              ast.getTrivialTypeSourceInfo(super_qt));
        }
      }
    }
  }

  // Ivars, added as ObjCIvarDecls (not FieldDecls).
  for (uint32_t i = 0; i < iface->GetNumFields(); ++i) {
    const ct::Field *field = iface->GetFieldAtIndex(i);
    clang::QualType ivar_qt = GenerateType(*ts, field->type.Get());
    if (ivar_qt.isNull())
      continue;
    EnsureComplete(ivar_qt);

    clang::Expr *bit_width = nullptr;
    if (field->IsBitfield()) {
      llvm::APInt width(ast.getIntWidth(ast.IntTy), field->bitfield_bit_size);
      clang::Expr *literal = clang::IntegerLiteral::Create(
          ast, width, ast.IntTy, clang::SourceLocation());
      bit_width = clang::ConstantExpr::Create(
          ast, literal, clang::APValue(llvm::APSInt(width)));
    }

    clang::IdentifierInfo *ident = nullptr;
    if (!field->name.GetName().empty())
      ident = &ast.Idents.get(field->name.GetName());

    auto *ivar =
        clang::ObjCIvarDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    ivar->setDeclContext(iface_decl);
    ivar->setDeclName(ident);
    ivar->setType(ivar_qt);
    ivar->setAccessControl(clang::ObjCIvarDecl::Public);
    if (bit_width)
      ivar->setBitWidth(bit_width);
    iface_decl->addDecl(ivar);
  }

  // Methods, added as ObjCMethodDecls so the expression parser can type-check
  // and lower message sends (`[obj sel:arg]` / `[Class sel:arg]`). Mirrors
  // TypeSystemClang::AddMethodToObjCObjectType. Methods are parsed lazily (like
  // C++ member functions), so make sure they're available first.
  ts->CompleteMemberFunctions(iface);

  // Prefer the imported module's method signatures over the debug-info/runtime
  // ones: a Clang module carries the real typedef'd types (`-(NSUInteger)length`,
  // `-(NSString *)scheme`), whereas the ObjC runtime only knows type encodings
  // and so reports `unsigned long long` / an opaque `id`. Merge by the full
  // `[+-][Class selector]` name (both paths format it identically), keeping any
  // method the module doesn't declare (e.g. from a class extension the runtime
  // saw). Only fires when a module was actually imported this session.
  llvm::StringSet<> seen_methods;
  TypeSystemCpp *module_ts = nullptr;
  if (ct::ObjCInterfaceType *module_iface =
          GetModuleObjCInterface(iface->GetName().GetName(), module_ts)) {
    for (uint32_t i = 0; i < module_iface->GetNumObjCMethods(); ++i) {
      const ct::ObjCMethod *method = module_iface->GetObjCMethodAtIndex(i);
      if (seen_methods.insert(method->name.GetName()).second)
        AddObjCMethod(iface_decl, *module_ts, *method);
    }
  }
  for (uint32_t i = 0; i < iface->GetNumObjCMethods(); ++i) {
    const ct::ObjCMethod *method = iface->GetObjCMethodAtIndex(i);
    if (seen_methods.insert(method->name.GetName()).second)
      AddObjCMethod(iface_decl, *ts, *method);
  }

  // The members were added directly, so no external source is needed to
  // enumerate them.
  iface_decl->setHasExternalLexicalStorage(false);
  iface_decl->setHasExternalVisibleStorage(false);
}

void ClangASTGenerator::AddObjCMethod(clang::ObjCInterfaceDecl *iface_decl,
                                      TypeSystemCpp &ts,
                                      const ct::ObjCMethod &method) {
  clang::ASTContext &ast = m_ast;
  llvm::StringRef full_name = method.name.GetName();

  // The full name is `[+-][Class selector-part(s)]`; the selector starts after
  // the first space.
  size_t space = full_name.find(' ');
  if (space == llvm::StringRef::npos)
    return;
  llvm::StringRef selector_str = full_name.substr(space + 1);
  selector_str.consume_back("]");

  // Split the selector into its identifier pieces, counting how many take an
  // argument (`sel:`). A unary selector (`length`) has zero args.
  llvm::SmallVector<const clang::IdentifierInfo *, 8> selector_idents;
  unsigned num_selectors_with_args = 0;
  llvm::StringRef rest = selector_str;
  while (!rest.empty()) {
    size_t colon = rest.find(':');
    if (colon == llvm::StringRef::npos) {
      selector_idents.push_back(&ast.Idents.get(rest));
      break;
    }
    ++num_selectors_with_args;
    selector_idents.push_back(&ast.Idents.get(rest.substr(0, colon)));
    rest = rest.substr(colon + 1);
  }
  if (selector_idents.empty())
    return;

  clang::Selector sel = ast.Selectors.getSelector(
      num_selectors_with_args ? selector_idents.size() : 0,
      selector_idents.data());

  // Build the (self/_cmd-stripped) function prototype for the return type and
  // parameter types.
  clang::QualType fn_qt = GenerateType(ts, method.type.Get());
  const auto *proto = fn_qt.isNull()
                          ? nullptr
                          : fn_qt->getAs<clang::FunctionProtoType>();
  if (!proto)
    return;
  if (proto->getNumParams() != num_selectors_with_args)
    return; // Corrupt debug info; give up on this method.

  const bool is_instance = !method.is_class_method;

  // A related-result-type method returns `instancetype`, so a class-method send
  // (`[NSURL URLWithString:...]`) types as the receiver class pointer rather
  // than a bare `id` (whose dereference is unsized). The cpp method carries an
  // `id` placeholder return; substitute `instancetype` and mark the decl.
  clang::QualType return_type = method.returns_instancetype
                                    ? ast.getObjCInstanceType()
                                    : proto->getReturnType();

  auto *method_decl =
      clang::ObjCMethodDecl::CreateDeserialized(ast, clang::GlobalDeclID());
  method_decl->setDeclName(sel);
  method_decl->setReturnType(return_type);
  method_decl->setDeclContext(iface_decl);
  method_decl->setInstanceMethod(is_instance);
  method_decl->setVariadic(method.is_variadic);
  method_decl->setPropertyAccessor(false);
  method_decl->setSynthesizedAccessorStub(false);
  method_decl->setImplicit(true);
  method_decl->setDefined(false);
  method_decl->setDeclImplementation(clang::ObjCImplementationControl::None);
  method_decl->setRelatedResultType(method.returns_instancetype);

  const unsigned num_args = proto->getNumParams();
  if (num_args > 0) {
    llvm::SmallVector<clang::ParmVarDecl *, 8> params;
    for (unsigned p = 0; p < num_args; ++p)
      params.push_back(clang::ParmVarDecl::Create(
          ast, method_decl, clang::SourceLocation(), clang::SourceLocation(),
          /*Id=*/nullptr, proto->getParamType(p), /*TInfo=*/nullptr,
          clang::SC_None, /*DefArg=*/nullptr));
    method_decl->setMethodParams(ast, params, {});
  }

  if (method.is_direct) {
    // Mirrors TypeSystemClang::AddMethodToObjCObjectType: mark the method
    // `objc_direct` so the expression parser's message-send codegen emits a
    // direct call to this implementation instead of an objc_msgSend dynamic
    // dispatch. A direct method is never registered with the ObjC runtime, so
    // dispatching it dynamically fails at runtime ("attempted to ... send it
    // an unrecognized selector"). Sema normally synthesizes the method's
    // implicit self/_cmd parameters while parsing; since there is no parsing
    // Sema here, create them manually (createImplicitParams) so direct-call
    // codegen (which reads them) doesn't crash.
    method_decl->addAttr(
        clang::ObjCDirectAttr::CreateImplicit(ast, clang::SourceLocation()));
    method_decl->createImplicitParams(ast, iface_decl);
  }

  iface_decl->addDecl(method_decl);
}

bool ClangASTGenerator::CompleteObjCInterface(
    clang::ObjCInterfaceDecl *iface_decl) {
  GenerationGuard guard(*this);
  if (!iface_decl)
    return false;
  if (m_objc_interfaces.find(iface_decl) == m_objc_interfaces.end())
    return false;
  PopulateObjCInterface(iface_decl);
  return true;
}

clang::NamedDecl *
ClangASTGenerator::LookupNestedType(const clang::RecordDecl *record_decl,
                                    llvm::StringRef name) {
  GenerationGuard guard(*this);
  auto it = m_records.find(record_decl);
  if (it == m_records.end())
    return nullptr;
  RecordInfo &info = *it->second;

  // Reuse a nested type we already resolved and parented into this record.
  if (auto cached = info.nested_types.find(name);
      cached != info.nested_types.end())
    return cached->second;

  ct::RecordType *rec = info.cpp_record;
  TypeSystemCpp &ts = *info.ts;
  // Make sure the record's members (including nested types) have been parsed
  // from debug info.
  ts.GetCompilerType(rec).GetCompleteType();

  ct::Type *nested = rec->GetNestedTypeWithName(name);
  if (!nested)
    return nullptr;

  // Generating the nested type places its clang decl in the DeclContext of its
  // cpp declaration context, which -- because a record is not modelled as a cpp
  // Namespace -- is the enclosing namespace (or the translation unit), not this
  // record. Re-parent it into this record so a qualified reference like
  // `Record::Nested` resolves to a member of the record. Generation only hands
  // out a forward declaration, so this does not complete the nested type (and
  // thus can't recurse through a self-referential nested type).
  clang::QualType nested_qt = GenerateType(ts, nested);
  if (nested_qt.isNull())
    return nullptr;
  clang::NamedDecl *nested_decl = nullptr;
  // Check for a typedef before a tag: a nested typedef aliasing a record (e.g.
  // `StructTypedef = S<float>`) generates a TypedefType, but getAs<TagType>()
  // sees through the alias to the underlying record's tag. We must re-parent
  // (and hand clang) the TypedefDecl named by the alias, not the aliased tag --
  // otherwise the qualified lookup returns a decl whose name mismatches.
  if (const auto *tdt = nested_qt->getAs<clang::TypedefType>())
    nested_decl = tdt->getDecl();
  else if (const auto *tt = nested_qt->getAs<clang::TagType>())
    nested_decl = tt->getDecl();
  if (!nested_decl)
    return nullptr;

  auto *decl = const_cast<clang::CXXRecordDecl *>(info.clang_decl);
  if (nested_decl->getDeclContext() != decl) {
    if (auto *old_ctx = nested_decl->getDeclContext())
      old_ctx->removeDecl(nested_decl);
    nested_decl->setDeclContext(decl);
    nested_decl->setLexicalDeclContext(decl);
    nested_decl->setAccess(clang::AS_public);
    decl->addDecl(nested_decl);
  }
  info.nested_types[name] = nested_decl;
  return nested_decl;
}

bool ClangASTGenerator::LayoutRecord(
    const clang::RecordDecl *record_decl, uint64_t &size, uint64_t &alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &base_offsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &vbase_offsets) {
  GenerationGuard guard(*this);
  auto it = m_records.find(record_decl);
  if (it == m_records.end())
    return false;
  RecordInfo &info = *it->second;
  ct::RecordType *rec = info.cpp_record;

  // Make sure the record is populated so we can iterate its clang fields.
  PopulateRecord(const_cast<clang::RecordDecl *>(record_decl));

  // PopulateRecord may have read the layout from a cross-module complete
  // definition rather than info.cpp_record (which can be a forward declaration
  // in its own module); use that same record for the size/base offsets.
  if (info.layout_record)
    rec = info.layout_record;

  uint64_t byte_size = rec->GetByteSize().value_or(0);
  size = byte_size * 8;

  // Prefer an explicitly-recorded alignment (e.g. from `alignas(...)` /
  // `__attribute__((aligned(N)))`, which DWARFASTParserCpp stores on the
  // record from DW_AT_alignment -- see TypeSystemCpp::GetTypeBitAlign, which
  // does the same for the non-expression-evaluator query path). Otherwise
  // derive a value consistent with the record's size (Clang requires
  // size % align == 0); for the standard-layout types produced from debug
  // info this reproduces the natural alignment.
  if (std::optional<uint64_t> align = rec->GetAlignInBits(); align && *align)
    alignment = *align;
  else {
    uint64_t align_bytes = 1;
    while (align_bytes * 2 <= 8 && byte_size % (align_bytes * 2) == 0)
      align_bytes *= 2;
    alignment = align_bytes * 8;
  }

  // Field offsets (in bits). PopulateRecord recorded the offset of every field
  // decl it added -- including the synthetic unnamed bitfields -- so report
  // those directly (a parallel walk of the cpp fields would miss the synthetic
  // ones and misalign the rest).
  for (const clang::FieldDecl *fd : record_decl->fields()) {
    auto offset_it = info.field_bit_offsets.find(fd);
    if (offset_it != info.field_bit_offsets.end())
      field_offsets[fd] = offset_it->second;
  }

  // Base-class offsets (in bytes), matching declaration order. A virtual base
  // has no constant offset (DWARF encodes it as a location expression), so it
  // must not be reported as a direct-base offset -- Clang computes the virtual
  // base layout itself from the record's size/vtable. Mirrors
  // DWARFASTParserClang, which likewise omits virtual bases from base_offsets
  // and leaves vbase_offsets empty.
  if (const auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(record_decl)) {
    uint32_t base_idx = 0;
    for (const clang::CXXBaseSpecifier &base : cxx->bases()) {
      if (base_idx >= rec->GetNumBaseClasses())
        break;
      const ct::BaseClass *cpp_base = rec->GetBaseClassAtIndex(base_idx++);
      if (cpp_base->is_virtual)
        continue;
      if (auto *base_rd = base.getType()->getAsCXXRecordDecl())
        base_offsets[base_rd] =
            clang::CharUnits::fromQuantity(cpp_base->byte_offset);
    }
  }
  (void)vbase_offsets;
  return true;
}
