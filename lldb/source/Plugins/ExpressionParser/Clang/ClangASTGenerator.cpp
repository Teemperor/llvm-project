//===-- ClangASTGenerator.cpp ---------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangASTGenerator.h"

#include "Plugins/TypeSystem/Cpp/Builder.h"
#include "Plugins/TypeSystem/Cpp/Context.h"
#include "Plugins/TypeSystem/Cpp/Namespace.h"
#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Host/FileSystem.h"
#include "lldb/Utility/LLDBLog.h"
#include "lldb/Utility/Log.h"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/CXXInheritance.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclarationName.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
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
static void noteReverse(llvm::DenseMap<void *, ct::Type *> &reverse,
                        clang::QualType qt, ct::Type *cpp_type) {
  if (qt.isNull())
    return;
  reverse[qt.getAsOpaquePtr()] = cpp_type;
  clang::QualType canonical = qt.getCanonicalType();
  if (canonical.getAsOpaquePtr() == qt.getAsOpaquePtr())
    reverse[canonical.getAsOpaquePtr()] = cpp_type;
}

void ClangASTGenerator::RegisterNamespace(const ct::Namespace *cpp_ns,
                                          clang::NamespaceDecl *clang_ns) {
  if (cpp_ns)
    m_namespaces[cpp_ns] = clang_ns;
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
  auto *nsd = clang::NamespaceDecl::Create(
      m_ast, parent, cpp_ns->IsInline(), clang::SourceLocation(),
      clang::SourceLocation(), ident, /*PrevDecl=*/nullptr, /*Nested=*/false);
  parent->addDecl(nsd);
  RegisterNamespace(cpp_ns, nsd);
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
  return GenerateType(*ts, type);
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

  // Synthesize a full definition for each record into the throwaway context.
  // Generate() only hands out a forward declaration (completion is normally
  // driven lazily by clang's external source, which this standalone context
  // has none of), so complete each record explicitly.
  ClangASTGenerator generator(ast);
  for (const CompilerType &record : records) {
    clang::QualType qt = generator.Generate(record);
    generator.EnsureComplete(qt);
  }

  // Completing a record only forward-declares the records it points to (a
  // pointer/reference member needs no definition). Those still-incomplete
  // decls kept the external-storage flags set in GenerateType, but this
  // standalone context has no external source to satisfy them -- the AST
  // dumper would assert when it tried to load their lexical decls. Clear the
  // flags on every generated record so the dumper treats an uncompleted one as
  // a plain forward declaration.
  for (auto &entry : generator.m_records) {
    clang::CXXRecordDecl *decl = entry.second->clang_decl;
    if (!decl->isCompleteDefinition()) {
      decl->setHasExternalLexicalStorage(false);
      decl->setHasExternalVisibleStorage(false);
    }
  }

  std::unique_ptr<clang::ASTConsumer> consumer = clang::CreateASTDumper(
      output, filter, /*DumpDecls=*/true, /*Deserialize=*/false,
      /*DumpLookups=*/false, /*DumpDeclTypes=*/false, clang::ADOF_Default);
  consumer->HandleTranslationUnit(ast);
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

clang::QualType ClangASTGenerator::GenerateType(TypeSystemCpp &ts,
                                                ct::Type *cpp_type) {
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

  if (auto *rec = llvm::dyn_cast<ct::RecordType>(cpp_type)) {
    // Records are created as forward declarations and completed on demand (see
    // PopulateRecord). This mirrors lazy DWARF parsing and keeps cycles finite.
    clang::TagTypeKind kind = rec->IsUnion()
                                  ? clang::TagTypeKind::Union
                                  : (llvm::isa<ct::ClassType>(rec)
                                         ? clang::TagTypeKind::Class
                                         : clang::TagTypeKind::Struct);
    auto *decl =
        clang::CXXRecordDecl::CreateDeserialized(ast, clang::GlobalDeclID());
    decl->setTagKind(kind);
    decl->setDeclContext(decl_ctx);
    llvm::StringRef name = unqualified_name(rec);
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
  } else if (auto *ptr = llvm::dyn_cast<ct::PointerType>(cpp_type)) {
    clang::QualType pointee;
    if (ct::Type *p = ptr->GetPointeeType())
      pointee = GenerateType(ts, p);
    else
      pointee = ast.VoidTy;
    if (!pointee.isNull())
      result = ast.getPointerType(pointee);
  } else if (auto *ref = llvm::dyn_cast<ct::ReferenceType>(cpp_type)) {
    clang::QualType pointee = GenerateType(ts, ref->GetPointeeType());
    if (!pointee.isNull())
      result = ref->IsRValue() ? ast.getRValueReferenceType(pointee)
                               : ast.getLValueReferenceType(pointee);
  } else if (auto *arr = llvm::dyn_cast<ct::ArrayType>(cpp_type)) {
    clang::QualType elem = GenerateType(ts, arr->GetElementType());
    if (!elem.isNull()) {
      if (std::optional<uint64_t> n = arr->GetNumElements())
        result = ast.getConstantArrayType(
            elem, llvm::APInt(64, *n), nullptr,
            clang::ArraySizeModifier::Normal, 0);
      else
        result = ast.getIncompleteArrayType(
            elem, clang::ArraySizeModifier::Normal, 0);
    }
  } else if (auto *td = llvm::dyn_cast<ct::TypedefType>(cpp_type)) {
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
  noteReverse(m_reverse, result, cpp_type);
  return result;
}

void ClangASTGenerator::PopulateRecord(clang::RecordDecl *record_decl) {
  auto it = m_records.find(record_decl);
  if (it == m_records.end())
    return;
  RecordInfo &info = *it->second;
  if (info.completed)
    return;
  info.completed = true;

  TypeSystemCpp &ts = *info.ts;
  ct::RecordType *rec = info.cpp_record;
  clang::CXXRecordDecl *decl = info.clang_decl;
  clang::ASTContext &ast = m_ast;

  // Make sure the record's members are parsed from debug info before we read
  // them out.
  CompilerType cpp_ct = ts.GetCompilerType(rec);
  cpp_ct.GetCompleteType();

  decl->startDefinition();

  // Base classes (C++ classes only). A base subobject is embedded by value, so
  // Clang requires its full definition when finalizing this record; complete
  // each base before wiring it up.
  if (rec->GetNumBaseClasses()) {
    std::vector<clang::CXXBaseSpecifier *> bases;
    for (uint32_t i = 0; i < rec->GetNumBaseClasses(); ++i) {
      const ct::BaseClass *base = rec->GetBaseClassAtIndex(i);
      clang::QualType base_qt = GenerateType(ts, base->type.Get());
      if (base_qt.isNull())
        continue;
      EnsureComplete(base_qt);
      bases.push_back(new (ast) clang::CXXBaseSpecifier(
          clang::SourceRange(), /*is_virtual=*/false, /*base_of_class=*/true,
          clang::AS_public, ast.getTrivialTypeSourceInfo(base_qt),
          clang::SourceLocation()));
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
    clang::QualType field_qt = GenerateType(ts, field->type.Get());
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
  ts.CompleteMemberFunctions(rec);
  for (uint32_t i = 0; i < rec->GetNumMemberFunctions(); ++i) {
    const ct::MemberFunction *mf = rec->GetMemberFunctionAtIndex(i);
    clang::QualType method_qt = GenerateType(ts, mf->type.Get());
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
    clang::QualType member_qt = GenerateType(ts, sm->type.Get());
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
                                    llvm::StringRef asm_label) {
  GenerationGuard guard(*this);
  clang::QualType function_qt = Generate(function_cpp_type);
  if (function_qt.isNull() || !function_qt->getAs<clang::FunctionProtoType>())
    return nullptr;
  clang::ASTContext &ast = m_ast;
  return BuildFunction(clang::DeclarationName(&ast.Idents.get(name)),
                       function_qt, asm_label);
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
ClangASTGenerator::BuildFunction(clang::DeclarationName name,
                                 clang::QualType function_qt,
                                 llvm::StringRef asm_label) {
  clang::ASTContext &ast = m_ast;
  auto *fd = clang::FunctionDecl::CreateDeserialized(ast, clang::GlobalDeclID());
  fd->setDeclContext(ast.getTranslationUnitDecl());
  fd->setDeclName(name);
  fd->setType(function_qt);
  fd->setStorageClass(clang::SC_Extern);
  fd->setConstexprKind(clang::ConstexprSpecKind::Unspecified);
  if (!asm_label.empty())
    fd->addAttr(clang::AsmLabelAttr::CreateImplicit(ast, asm_label));
  BuildParams(fd, function_qt);
  ast.getTranslationUnitDecl()->addDecl(fd);
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
  if (auto *rd = type->getAsCXXRecordDecl())
    CompleteRecord(rd);
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
  if (const auto *tt = nested_qt->getAs<clang::TagType>())
    nested_decl = tt->getDecl();
  else if (const auto *tdt = nested_qt->getAs<clang::TypedefType>())
    nested_decl = tdt->getDecl();
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

  uint64_t byte_size = rec->GetByteSize().value_or(0);
  size = byte_size * 8;

  // The cpp_typesystem model doesn't carry alignment; derive a value that is
  // consistent with the record's size (Clang requires size % align == 0). For
  // the standard-layout types produced from debug info this reproduces the
  // natural alignment.
  uint64_t align_bytes = 1;
  while (align_bytes * 2 <= 8 && byte_size % (align_bytes * 2) == 0)
    align_bytes *= 2;
  alignment = align_bytes * 8;

  // Field offsets (in bits). PopulateRecord recorded the offset of every field
  // decl it added -- including the synthetic unnamed bitfields -- so report
  // those directly (a parallel walk of the cpp fields would miss the synthetic
  // ones and misalign the rest).
  for (const clang::FieldDecl *fd : record_decl->fields()) {
    auto offset_it = info.field_bit_offsets.find(fd);
    if (offset_it != info.field_bit_offsets.end())
      field_offsets[fd] = offset_it->second;
  }

  // Base-class offsets (in bytes), matching declaration order.
  if (const auto *cxx = llvm::dyn_cast<clang::CXXRecordDecl>(record_decl)) {
    uint32_t base_idx = 0;
    for (const clang::CXXBaseSpecifier &base : cxx->bases()) {
      if (base_idx >= rec->GetNumBaseClasses())
        break;
      const ct::BaseClass *cpp_base = rec->GetBaseClassAtIndex(base_idx++);
      if (auto *base_rd = base.getType()->getAsCXXRecordDecl())
        base_offsets[base_rd] =
            clang::CharUnits::fromQuantity(cpp_base->byte_offset);
    }
  }
  (void)vbase_offsets;
  return true;
}

CompilerType ClangASTGenerator::MapClangTypeToCpp(clang::QualType qt,
                                                  TypeSystemCpp &result_ts) {
  if (qt.isNull())
    return {};

  // `typeof`/`decltype` sugar (`typeof (i)`, `__typeof__(i)`, `decltype(i)`)
  // wraps a resolved underlying type. None of these sugar nodes are in the
  // reverse map (we never generate them), so map the underlying type and wrap
  // it in display sugar preserving the source spelling -- mirroring
  // TypeSystemClang, whose display name for `decltype(i) j` stays `decltype(i)`
  // while the canonical type desugars to `int`.
  if (llvm::isa<clang::TypeOfExprType, clang::TypeOfType, clang::DecltypeType>(
          qt.getTypePtr())) {
    if (CompilerType inner =
            MapClangTypeToCpp(qt.getCanonicalType(), result_ts)) {
      std::string spelling = qt.getAsString(m_ast.getPrintingPolicy());
      if (!spelling.empty())
        return cpp_typesystem::Builder(result_ts).CreateElaboratedType(
            ConstString(spelling), inner);
      return inner;
    }
    return {};
  }

  // Types we generated (including cv-qualified variants) map back directly.
  auto direct = m_reverse.find(qt.getAsOpaquePtr());
  auto find = direct;
  if (find == m_reverse.end())
    find = m_reverse.find(qt.getCanonicalType().getAsOpaquePtr());
  if (find != m_reverse.end()) {
    // Map the recovered type into result_ts (the scratch TypeSystemCpp that
    // owns expression result/persistent types). Keeping result types in the
    // scratch TS -- rather than their parsing module's TS -- means a type
    // reachable only through a pointer in the result stays a forward
    // declaration (it has no SymbolFile in the scratch TS to complete it),
    // matching the lazy-completion contract.
    CompilerType mapped = result_ts.GetCompilerType(find->second);

    // The parser may have kept elaborated/spelling sugar around the type the
    // user wrote (e.g. `::Struct`, `$V< ::Struct>`). It only reached us via the
    // canonical fallback (its own opaque pointer wasn't in the map), which means
    // the spelling differs from the canonical type. Preserve that spelling as
    // pure display sugar, mirroring TypeSystemClang: the display name shows
    // `::Struct` while the canonical type name stays `Struct` so name-based
    // formatters still match.
    if (mapped && direct == m_reverse.end() &&
        qt.getAsOpaquePtr() != qt.getCanonicalType().getAsOpaquePtr()) {
      std::string spelling = qt.getAsString(m_ast.getPrintingPolicy());
      if (!spelling.empty())
        return cpp_typesystem::Builder(result_ts).CreateElaboratedType(
            ConstString(spelling), mapped);
    }
    return mapped;
  }

  // Preserve cv-qualifiers the parser applied but that we didn't generate
  // ourselves (e.g. the `const` of the `const char *` parameter in a function
  // cast): map the unqualified type and re-wrap it.
  if (qt.hasLocalQualifiers()) {
    if (CompilerType inner =
            MapClangTypeToCpp(qt.getLocalUnqualifiedType(), result_ts))
      return cpp_typesystem::Builder(result_ts).CreateCVQualifiedType(
          inner, qt.isLocalConstQualified(), qt.isLocalVolatileQualified());
    return {};
  }

  // Builtin types (int, unsigned long, bool, ...) the parser created on its own
  // -- e.g. the result type of `1 + 1`, a `sizeof` expression, or a cast -- are
  // never in the reverse map because we didn't generate them. Map them onto the
  // corresponding TypeSystemCpp builtin so the result type can be sized.
  if (const auto *bt = qt->getAs<clang::BuiltinType>()) {
    lldb::BasicType basic = lldb::eBasicTypeInvalid;
    switch (bt->getKind()) {
    case clang::BuiltinType::Void:
      basic = lldb::eBasicTypeVoid;
      break;
    case clang::BuiltinType::Bool:
      basic = lldb::eBasicTypeBool;
      break;
    case clang::BuiltinType::Char_U:
    case clang::BuiltinType::Char_S:
      basic = lldb::eBasicTypeChar;
      break;
    case clang::BuiltinType::UChar:
      basic = lldb::eBasicTypeUnsignedChar;
      break;
    case clang::BuiltinType::SChar:
      basic = lldb::eBasicTypeSignedChar;
      break;
    case clang::BuiltinType::WChar_U:
    case clang::BuiltinType::WChar_S:
      basic = lldb::eBasicTypeWChar;
      break;
    case clang::BuiltinType::Char8:
      basic = lldb::eBasicTypeChar8;
      break;
    case clang::BuiltinType::Char16:
      basic = lldb::eBasicTypeChar16;
      break;
    case clang::BuiltinType::Char32:
      basic = lldb::eBasicTypeChar32;
      break;
    case clang::BuiltinType::Short:
      basic = lldb::eBasicTypeShort;
      break;
    case clang::BuiltinType::UShort:
      basic = lldb::eBasicTypeUnsignedShort;
      break;
    case clang::BuiltinType::Int:
      basic = lldb::eBasicTypeInt;
      break;
    case clang::BuiltinType::UInt:
      basic = lldb::eBasicTypeUnsignedInt;
      break;
    case clang::BuiltinType::Long:
      basic = lldb::eBasicTypeLong;
      break;
    case clang::BuiltinType::ULong:
      basic = lldb::eBasicTypeUnsignedLong;
      break;
    case clang::BuiltinType::LongLong:
      basic = lldb::eBasicTypeLongLong;
      break;
    case clang::BuiltinType::ULongLong:
      basic = lldb::eBasicTypeUnsignedLongLong;
      break;
    case clang::BuiltinType::Int128:
      basic = lldb::eBasicTypeInt128;
      break;
    case clang::BuiltinType::UInt128:
      basic = lldb::eBasicTypeUnsignedInt128;
      break;
    case clang::BuiltinType::Float:
      basic = lldb::eBasicTypeFloat;
      break;
    case clang::BuiltinType::Double:
      basic = lldb::eBasicTypeDouble;
      break;
    case clang::BuiltinType::LongDouble:
      basic = lldb::eBasicTypeLongDouble;
      break;
    default:
      break;
    }
    if (basic != lldb::eBasicTypeInvalid)
      return result_ts.GetBasicTypeFromAST(basic);
  }

  // Simple derived types (a reference or pointer created by the parser itself,
  // e.g. the `T &` VarDecls we synthesize for locals or the result
  // synthesizer's pointer wrappers) aren't in the map. Reconstruct them in
  // result_ts from their pointee, which is looked up recursively.
  if (qt->isReferenceType()) {
    if (CompilerType pointee = MapClangTypeToCpp(qt->getPointeeType(), result_ts))
      return cpp_typesystem::Builder(result_ts).CreateReferenceType(
          pointee, qt->isRValueReferenceType());
  } else if (qt->isPointerType()) {
    if (CompilerType pointee = MapClangTypeToCpp(qt->getPointeeType(), result_ts))
      return cpp_typesystem::Builder(result_ts).CreatePointerType(pointee);
  } else if (const auto *cx = qt->getAs<clang::ComplexType>()) {
    // A complex value produced by the expression (e.g. `a + (1.0f + 2.0fi)`)
    // maps back to a TypeSystemCpp ComplexType over its mapped element.
    if (CompilerType element =
            MapClangTypeToCpp(cx->getElementType(), result_ts))
      return cpp_typesystem::Builder(result_ts).CreateComplexType(element);
  } else if (const auto *fpt = qt->getAs<clang::FunctionProtoType>()) {
    // A function type the parser formed (e.g. the pointee of a function-pointer
    // cast result). Rebuild it so a pointer to it can be sized/stored.
    CompilerType ret = MapClangTypeToCpp(fpt->getReturnType(), result_ts);
    cpp_typesystem::Builder builder(result_ts);
    CompilerType fn = builder.CreateFunctionType(ret, fpt->isVariadic());
    for (clang::QualType param : fpt->param_types()) {
      CompilerType cpp_param = MapClangTypeToCpp(param, result_ts);
      if (!cpp_param)
        return {};
      builder.AddParameter(fn, cpp_param);
    }
    return fn;
  } else if (const clang::ArrayType *at = m_ast.getAsArrayType(qt)) {
    // An array the parser formed (e.g. the `const char16_t[6]` result type of a
    // `u"hello"` string literal). Map the element type recursively and rebuild
    // the array so the result type can be sized. A constant array carries its
    // element count; an incomplete array (`char[]`) has no bound.
    if (CompilerType element =
            MapClangTypeToCpp(at->getElementType(), result_ts)) {
      std::optional<uint64_t> num_elements;
      if (const auto *cat = llvm::dyn_cast<clang::ConstantArrayType>(at))
        num_elements = cat->getSize().getZExtValue();
      return cpp_typesystem::Builder(result_ts).CreateArrayType(element,
                                                                num_elements);
    }
  }
  return {};
}
