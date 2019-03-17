//===-- StdModuleHandler.cpp ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/StdModuleHandler.h"

#include "clang/Sema/Lookup.h"
#include "lldb/Symbol/ClangASTContext.h"
#include "llvm/Support/Error.h"


using namespace lldb_private;
using namespace clang;

static void makeScopes(Sema &sema,
                          DeclContext *ctxt,
                       std::vector<Scope *> &result) {
  if (auto parent = ctxt->getParent()) {
    makeScopes(sema, parent, result);

    Scope *scope = new Scope(result.back(),
                                           Scope::DeclScope,
                                           sema.getDiagnostics());
    scope->setEntity(ctxt);
    result.push_back(scope);
  } else
    result.push_back(sema.TUScope);
}

static std::unique_ptr<LookupResult> buildLookupForCtxt(Sema &sema,
                                                      llvm::StringRef name,
                                                      DeclContext *ctxt) {
  IdentifierInfo &ident =
      sema.getASTContext().Idents.get(name);

  std::unique_ptr<LookupResult> lookup_result;
  lookup_result.reset(new LookupResult(
      sema, DeclarationName(&ident),
      SourceLocation(), Sema::LookupOrdinaryName));

  std::vector<Scope *> scopes;
  makeScopes(sema, ctxt, scopes);


  sema.LookupName(*lookup_result, scopes.back());

  // Delete all the allocated scopes beside the translation unit scope (which
  // has depth 0.
  for (Scope *s : scopes)
    if (s->getDepth() != 0)
      delete s;

  return lookup_result;
}

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

static llvm::Expected<DeclContext *> getEqualLocalDeclContext(Sema &sema,
                                                DeclContext *foreign_ctxt) {

  while (foreign_ctxt && foreign_ctxt->isInlineNamespace())
    foreign_ctxt = foreign_ctxt->getParent();

  if (!foreign_ctxt)
    return sema.getASTContext().getTranslationUnitDecl();

  llvm::Expected<DeclContext *> parent = getEqualLocalDeclContext(sema, foreign_ctxt->getParent());
  if (!parent)
    return parent;

  if (foreign_ctxt->isNamespace()) {
    NamedDecl *ns = llvm::dyn_cast<NamedDecl>(foreign_ctxt);
    llvm::StringRef ns_name = ns->getName();

    auto lookup_result = buildLookupForCtxt(sema, ns_name, *parent);
    for (NamedDecl *named_decl : *lookup_result) {
      if (named_decl->getName() != ns_name)
        continue;
      if (named_decl->getKind() != ns->getKind())
        continue;

      if (DeclContext *DC = llvm::dyn_cast<DeclContext>(named_decl))
        return DC->getPrimaryContext();
    }
    return llvm::make_error<MissingDeclContext>(foreign_ctxt,
                                                "Couldn't find namespace " +
                                                ns->getQualifiedNameAsString());
  }
  if (foreign_ctxt->isTranslationUnit())
    return sema.getASTContext().getTranslationUnitDecl();

  return llvm::make_error<MissingDeclContext>(foreign_ctxt, "Unknown context ");
}

StdTemplateSpecializer::StdTemplateSpecializer(ASTImporter &importer,
                                               ASTContext *target)
  : m_importer(importer), m_sema(ClangASTContext::GetASTContext(target)->getSema()),
    m_supported_templates({"vector", "allocator"}){
  m_importer.Chain = this;
}

llvm::Optional<Decl *> StdTemplateSpecializer::tryInstantiateStdTemplate(Decl *d) {
  // If we don't have a template to instiantiate, then there is nothing to do.
  auto td = dyn_cast<ClassTemplateSpecializationDecl>(d);
  if (!td)
    return {};

  // We only care about templates in the std namespace.
  if (!td->getDeclContext()->isStdNamespace())
    return {};

  if (m_supported_templates.find(td->getName()) == m_supported_templates.end())
    return {};

  // Find the local DeclContext that corresponds to the DeclContext of our
  // decl we want to import.
  auto to_context = getEqualLocalDeclContext(*m_sema, td->getDeclContext());
  if (!to_context) {
    return {};
  }

  // Look up the template in our local context.
  std::unique_ptr<LookupResult> lookup = buildLookupForCtxt(*m_sema,
                                                            td->getName(),
                                                            *to_context);

  ClassTemplateDecl *new_class_template = nullptr;
  for (auto LD : *lookup) {
    if ((new_class_template = dyn_cast<ClassTemplateDecl>(LD)))
      break;
  }
  if (!new_class_template)
    return {};

  // Import the foreign template arguments.
  auto &foreign_args = td->getTemplateInstantiationArgs();
  llvm::SmallVector<TemplateArgument, 4> imported_args;

  for (const TemplateArgument& arg : foreign_args.asArray()) {
    switch(arg.getKind()) {
      case TemplateArgument::Type: {
        QualType our_type = m_importer.Import(arg.getAsType());
        imported_args.push_back(TemplateArgument(our_type));
        break;
      }
      case TemplateArgument::Integral: {
        llvm::APSInt integral = arg.getAsIntegral();
        QualType our_type = m_importer.Import(arg.getIntegralType());
        imported_args.push_back(TemplateArgument(d->getASTContext(), integral,
                                                 our_type));
        break;
      }
      default:
        return {};
    }
  }

  // Find the class template specialization declaration that
  // corresponds to these arguments.
  void *InsertPos = nullptr;
  ClassTemplateSpecializationDecl *found_decl
      = new_class_template->findSpecialization(imported_args, InsertPos);
  if (!found_decl) {
    // This is the first time we have referenced this class template
    // specialization. Create the canonical declaration and add it to
    // the set of specializations.
    found_decl = ClassTemplateSpecializationDecl::Create(m_sema->getASTContext(),
                                                         new_class_template->getTemplatedDecl()->getTagKind(),
                                                         new_class_template->getDeclContext(),
                                                         new_class_template->getTemplatedDecl()->getLocation(),
                                                         new_class_template->getLocation(),
                                                         new_class_template,
                                                         imported_args, nullptr);
    new_class_template->AddSpecialization(found_decl, InsertPos);
    if (new_class_template->isOutOfLine())
      found_decl->setLexicalDeclContext(new_class_template->getLexicalDeclContext());
  }

  if (found_decl)
    return found_decl;

  return {};
}

llvm::Optional<Decl *> StdTemplateSpecializer::Import(Decl *d) {
  if (!isValid())
    return {};
  if (auto result = tryInstantiateStdTemplate(d))
    return result;

  return {};
}
