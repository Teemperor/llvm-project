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

using namespace lldb_private;
using namespace clang;

static void makeScopes(clang::Sema &sema,
                          clang::DeclContext *ctxt,
                       std::vector<clang::Scope *> &result) {
  if (auto parent = ctxt->getParent()) {
    makeScopes(sema, parent, result);

    clang::Scope *scope = new clang::Scope(result.back(),
                                           clang::Scope::DeclScope,
                                           sema.getDiagnostics());
    scope->setEntity(ctxt);
    result.push_back(scope);
  } else
    result.push_back(sema.TUScope);
}

static std::unique_ptr<clang::LookupResult> do_lookup(clang::Sema &sema,
                                                      llvm::StringRef name,
                                                      clang::DeclContext *ctxt) {
  clang::IdentifierInfo &ident =
      sema.getASTContext().Idents.get(name);

  std::unique_ptr<clang::LookupResult> lookup_result;
  lookup_result.reset(new clang::LookupResult(
      sema, clang::DeclarationName(&ident),
      clang::SourceLocation(), clang::Sema::LookupOrdinaryName));

  std::vector<clang::Scope *> scopes;
  makeScopes(sema, ctxt, scopes);
  sema.LookupName(*lookup_result, scopes.back());
  // TODO: free memory of scopes

  for (clang::Scope *s : scopes)
    if (s->getDepth() != 0)
      delete s;

  return lookup_result;
}

static clang::DeclContext *getEqualLocalContext(clang::Sema &sema,
                                                clang::DeclContext *foreign_ctxt) {

  while (foreign_ctxt && foreign_ctxt->isInlineNamespace())
    foreign_ctxt = foreign_ctxt->getParent();

  if (!foreign_ctxt)
    return sema.getASTContext().getTranslationUnitDecl();

  clang::DeclContext *parent = getEqualLocalContext(sema, foreign_ctxt->getParent());

  if (foreign_ctxt->isNamespace()) {
    clang::NamedDecl *ns = llvm::dyn_cast<clang::NamedDecl>(foreign_ctxt);
    llvm::StringRef ns_name = ns->getName();

    auto lookup_result = do_lookup(sema, ns_name, parent);
    for (clang::NamedDecl *named_decl : *lookup_result) {
      if (named_decl->getName() != ns_name)
        continue;
      if (named_decl->getKind() != ns->getKind())
        continue;

      if (clang::DeclContext *DC = llvm::dyn_cast<clang::DeclContext>(named_decl))
        return DC->getPrimaryContext();
    }
    llvm::errs() << "CANT " << ns->getNameAsString() << "\n";
  }
  return sema.getASTContext().getTranslationUnitDecl();
}

StdTemplateSpecializer::StdTemplateSpecializer(ASTImporter &importer,
                                               ASTContext *target)
  : m_importer(importer), m_sema(ClangASTContext::GetASTContext(target)->getSema()) {
  m_importer.Chain = this;
}

llvm::Optional<Decl *> StdTemplateSpecializer::Import(clang::Decl *d) {
  if (!isValid())
    return {};

  auto *decl = dyn_cast<clang::RecordDecl>(d);
  if (!decl)
    return {};

  //llvm::errs() << "importing std decl\n";
  //llvm::errs() << decl->getQualifiedNameAsString() << "\n";
  //decl->dumpColor();

  clang::DeclContext *ctxt = decl->getDeclContext();
  if (!ctxt->isStdNamespace())
    return {};

  auto temp = llvm::dyn_cast<clang::ClassTemplateSpecializationDecl>(decl);
  if (!temp)
    return {};

  clang::ClassTemplateDecl *temp_base = temp->getSpecializedTemplate();
  if (!temp_base)
    return {};

  auto &foreign_args = temp->getTemplateInstantiationArgs();
  llvm::SmallVector<clang::TemplateArgument, 4> imported_args;

  for (const clang::TemplateArgument& T : foreign_args.asArray()) {
    clang::QualType our_type = m_importer.Import(T.getAsType());
    imported_args.push_back(clang::TemplateArgument(our_type));
  }

  clang::ClassTemplateDecl *new_class_template = nullptr;

  auto lookup = do_lookup(*m_sema, decl->getName(), getEqualLocalContext(*m_sema, decl->getDeclContext()));
  for (auto LD : *lookup) {
    if ((new_class_template = dyn_cast<clang::ClassTemplateDecl>(LD)))
      break;
  }
  if (!new_class_template)
    return {};

  // Find the class template specialization declaration that
  // corresponds to these arguments.
  void *InsertPos = nullptr;
  clang::ClassTemplateSpecializationDecl *found_decl
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
