//===-- CxxModuleHandler.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ExpressionParser/Clang/CxxModuleHandler.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include "lldb/Utility/Log.h"
#include "clang/Sema/Lookup.h"
#include "llvm/Support/Error.h"

using namespace lldb_private;
using namespace clang;

CxxModuleHandler::CxxModuleHandler(ASTImporter &importer, ASTContext *target)
    : m_importer(&importer),
      m_sema(TypeSystemClang::GetASTContext(target)->getSema()) {

  const std::initializer_list<const char *> supported_templates = {
      // containers
      "deque",
      "forward_list",
      "list",
      "queue",
      "stack",
      "vector",
      "basic_string",
      // pointers
      "shared_ptr",
      "unique_ptr",
      "weak_ptr",
      // utility
      "allocator",
      "pair",
      "char_traits",
  };
  m_supported_templates.insert(supported_templates.begin(),
                               supported_templates.end());

  const std::initializer_list<const char *> supported_typedefs = {
      "string", "wstring", "u16string", "u32string"};
  m_supported_named_decls.insert(supported_typedefs.begin(),
                                 supported_typedefs.end());
}

/// Builds a list of scopes that point into the given context.
///
/// \param sema The sema that will be using the scopes.
/// \param ctxt The context that the scope should look into.
/// \param result A list of scopes. The scopes need to be freed by the caller
///               (except the TUScope which is owned by the sema).
static void makeScopes(Sema &sema, DeclContext *ctxt,
                       std::vector<Scope *> &result) {
  // FIXME: The result should be a list of unique_ptrs, but the TUScope makes
  // this currently impossible as it's owned by the Sema.

  if (auto parent = ctxt->getParent()) {
    makeScopes(sema, parent, result);

    Scope *scope =
        new Scope(result.back(), Scope::DeclScope, sema.getDiagnostics());
    scope->setEntity(ctxt);
    result.push_back(scope);
  } else
    result.push_back(sema.TUScope);
}

/// Uses the Sema to look up the given name in the given DeclContext.
static std::unique_ptr<LookupResult>
emulateLookupInCtxt(Sema &sema, llvm::StringRef name, DeclContext *ctxt) {
  IdentifierInfo &ident = sema.getASTContext().Idents.get(name);

  std::unique_ptr<LookupResult> lookup_result;
  lookup_result = std::make_unique<LookupResult>(sema, DeclarationName(&ident),
                                                 SourceLocation(),
                                                 Sema::LookupOrdinaryName);

  // Usually during parsing we already encountered the scopes we would use. But
  // here don't have these scopes so we have to emulate the behavior of the
  // Sema during parsing.
  std::vector<Scope *> scopes;
  makeScopes(sema, ctxt, scopes);

  // Now actually perform the lookup with the sema.
  sema.LookupName(*lookup_result, scopes.back());

  // Delete all the allocated scopes beside the translation unit scope (which
  // has depth 0).
  for (Scope *s : scopes)
    if (s->getDepth() != 0)
      delete s;

  return lookup_result;
}

/// Error class for handling problems when finding a certain DeclContext.
struct MissingDeclContext : public llvm::ErrorInfo<MissingDeclContext> {

  static char ID;

  MissingDeclContext(DeclContext *context, std::string error)
      : m_context(context), m_error(error) {}

  DeclContext *m_context;
  std::string m_error;

  void log(llvm::raw_ostream &OS) const override {
    OS << llvm::formatv("error when reconstructing context of kind {0}:{1}",
                        m_context->getDeclKindName(), m_error);
  }

  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }
};

char MissingDeclContext::ID = 0;

/// Given a foreign decl context, this function finds the equivalent local
/// decl context in the ASTContext of the given Sema. Potentially deserializes
/// decls from the 'std' module if necessary.
static llvm::Expected<DeclContext *>
getEqualLocalDeclContext(Sema &sema, DeclContext *foreign_ctxt) {

  // Inline namespaces don't matter for lookups, so let's skip them.
  while (foreign_ctxt && foreign_ctxt->isInlineNamespace())
    foreign_ctxt = foreign_ctxt->getParent();

  // If the foreign context is the TU, we just return the local TU.
  if (foreign_ctxt->isTranslationUnit())
    return sema.getASTContext().getTranslationUnitDecl();

  // Recursively find/build the parent DeclContext.
  llvm::Expected<DeclContext *> parent =
      getEqualLocalDeclContext(sema, foreign_ctxt->getParent());
  if (!parent)
    return parent;

  // We currently only support building namespaces.
  if (foreign_ctxt->isNamespace()) {
    NamedDecl *ns = llvm::dyn_cast<NamedDecl>(foreign_ctxt);
    llvm::StringRef ns_name = ns->getName();

    auto lookup_result = emulateLookupInCtxt(sema, ns_name, *parent);
    for (NamedDecl *named_decl : *lookup_result) {
      if (DeclContext *DC = llvm::dyn_cast<DeclContext>(named_decl))
        return DC->getPrimaryContext();
    }
    return llvm::make_error<MissingDeclContext>(
        foreign_ctxt,
        "Couldn't find namespace " + ns->getQualifiedNameAsString());
  }

  return llvm::make_error<MissingDeclContext>(foreign_ctxt, "Unknown context ");
}

/// Returns true iff tryInstantiateStdTemplate supports instantiating a template
/// with the given template arguments.
static bool templateArgsAreSupported(ArrayRef<TemplateArgument> a) {
  for (const TemplateArgument &arg : a) {
    switch (arg.getKind()) {
    case TemplateArgument::Type:
    case TemplateArgument::Integral:
      break;
    default:
      // TemplateArgument kind hasn't been handled yet.
      return false;
    }
  }
  return true;
}

/// Constructor function for Clang declarations. Ensures that the created
/// declaration is registered with the ASTImporter.
template <typename T, typename... Args>
T *createDecl(ASTImporter &importer, Decl *from_d, Args &&... args) {
  T *to_d = T::Create(std::forward<Args>(args)...);
  importer.RegisterImportedDecl(from_d, to_d);
  return to_d;
}

template <typename T>
T *findDeclInTargetAST(clang::Sema &sema, clang::NamedDecl *nd) {
  const llvm::StringRef name = nd->getName();
  DeclContext *from_dc = nd->getDeclContext();
  Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS);

  // Find the local DeclContext that corresponds to the DeclContext of our
  // decl we want to import.
  llvm::Expected<DeclContext *> to_context =
      getEqualLocalDeclContext(sema, from_dc);
  if (!to_context) {
    LLDB_LOG_ERROR(log, to_context.takeError(),
                   "Got error while searching equal local DeclContext for decl "
                   "'{1}':\n{0}",
                   name);
    return nullptr;
  }

  // Look up the template in our local context.
  std::unique_ptr<LookupResult> lookup =
      emulateLookupInCtxt(sema, name, *to_context);

  T *found_decl = nullptr;
  for (auto LD : *lookup) {
    if ((found_decl = dyn_cast<T>(LD)))
      break;
  }
  return found_decl;
}

llvm::Optional<Decl *> CxxModuleHandler::trySubstituteNamedDecl(Decl *d) {
  // We only support decls with an identifier name.
  auto nd = dyn_cast<NamedDecl>(d);
  if (!nd || !nd->getDeclName().isIdentifier())
    return llvm::None;

  // We only care about decls in the std namespace.
  if (!nd->getDeclContext()->isStdNamespace())
    return llvm::None;

  // Only substitute types that we explicitly allow to be substituted.
  if (!m_supported_named_decls.contains(nd->getName()))
    return llvm::None;

  NamedDecl *result = findDeclInTargetAST<NamedDecl>(*m_sema, nd);

  if (!result)
    return llvm::None;

  // Register the found decl it with the ASTImporter.
  m_importer->RegisterImportedDecl(d, result);
  return result;
}

llvm::Optional<Decl *> CxxModuleHandler::tryInstantiateStdTemplate(Decl *d) {
  Log *log = lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_EXPRESSIONS);

  // If we don't have a template to instiantiate, then there is nothing to do.
  auto td = dyn_cast<ClassTemplateSpecializationDecl>(d);
  if (!td)
    return llvm::None;

  // We only care about templates in the std namespace.
  if (!td->getDeclContext()->isStdNamespace())
    return llvm::None;

  // We have a list of supported template names.
  if (!m_supported_templates.contains(td->getName()))
    return llvm::None;

  // Early check if we even support instantiating this template. We do this
  // before we import anything into the target AST.
  auto &foreign_args = td->getTemplateInstantiationArgs();
  if (!templateArgsAreSupported(foreign_args.asArray()))
    return llvm::None;

  // Find the template that needs to be instantiated in the target AST.
  ClassTemplateDecl *new_class_template =
      findDeclInTargetAST<ClassTemplateDecl>(*m_sema, td);
  if (!new_class_template)
    return llvm::None;

  // Import the foreign template arguments.
  llvm::SmallVector<TemplateArgument, 4> imported_args;

  // If this logic is changed, also update templateArgsAreSupported.
  for (const TemplateArgument &arg : foreign_args.asArray()) {
    switch (arg.getKind()) {
    case TemplateArgument::Type: {
      llvm::Expected<QualType> type = m_importer->Import(arg.getAsType());
      if (!type) {
        LLDB_LOG_ERROR(log, type.takeError(), "Couldn't import type: {0}");
        return llvm::None;
      }
      imported_args.push_back(TemplateArgument(*type));
      break;
    }
    case TemplateArgument::Integral: {
      llvm::APSInt integral = arg.getAsIntegral();
      llvm::Expected<QualType> type =
          m_importer->Import(arg.getIntegralType());
      if (!type) {
        LLDB_LOG_ERROR(log, type.takeError(), "Couldn't import type: {0}");
        return llvm::None;
      }
      imported_args.push_back(
          TemplateArgument(d->getASTContext(), integral, *type));
      break;
    }
    default:
      assert(false && "templateArgsAreSupported not updated?");
    }
  }

  // Find the class template specialization declaration that
  // corresponds to these arguments.
  void *InsertPos = nullptr;
  ClassTemplateSpecializationDecl *result =
      new_class_template->findSpecialization(imported_args, InsertPos);

  if (result) {
    // We found an existing specialization in the module that fits our arguments
    // so we can treat it as the result and register it with the ASTImporter.
    m_importer->RegisterImportedDecl(d, result);
    return result;
  }

  // Instantiate the template.
  result = createDecl<ClassTemplateSpecializationDecl>(
      *m_importer, d, m_sema->getASTContext(),
      new_class_template->getTemplatedDecl()->getTagKind(),
      new_class_template->getDeclContext(),
      new_class_template->getTemplatedDecl()->getLocation(),
      new_class_template->getLocation(), new_class_template, imported_args,
      nullptr);

  new_class_template->AddSpecialization(result, InsertPos);
  if (new_class_template->isOutOfLine())
    result->setLexicalDeclContext(
        new_class_template->getLexicalDeclContext());
  return result;
}

llvm::Optional<Decl *> CxxModuleHandler::Import(Decl *d) {
  if (!isValid())
    return {};

  if (llvm::Optional<Decl *> r = trySubstituteNamedDecl(d))
    return r;

  return tryInstantiateStdTemplate(d);
}
