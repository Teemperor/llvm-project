//===-- ClangExternalASTSourceCallbacks.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ExpressionParser/Clang/ClangExternalASTSourceCallbacks.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"

#include "clang/AST/Decl.h"
#include "clang/AST/DeclObjC.h"

using namespace lldb_private;

char ClangExternalASTSourceCallbacks::ID;

void ClangExternalASTSourceCallbacks::CompleteType(clang::TagDecl *tag_decl) {
  m_ast.CompleteTagDecl(tag_decl);
}

void ClangExternalASTSourceCallbacks::CompleteType(
    clang::ObjCInterfaceDecl *objc_decl) {
  m_ast.CompleteObjCInterfaceDecl(objc_decl);
}

bool ClangExternalASTSourceCallbacks::layoutRecordType(
    const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &BaseOffsets,
    llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
        &VirtualBaseOffsets) {
  return m_ast.LayoutRecordType(Record, Size, Alignment, FieldOffsets,
                                BaseOffsets, VirtualBaseOffsets);
}

void ClangExternalASTSourceCallbacks::FindExternalLexicalDecls(
    const clang::DeclContext *decl_ctx,
    llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
    llvm::SmallVectorImpl<clang::Decl *> &decls) {
  if (decl_ctx) {
    clang::TagDecl *tag_decl = llvm::dyn_cast<clang::TagDecl>(
        const_cast<clang::DeclContext *>(decl_ctx));
    if (tag_decl)
      CompleteType(tag_decl);
  }
}

bool ClangExternalASTSourceCallbacks::FindExternalVisibleDeclsByName(
    const clang::DeclContext *DC, clang::DeclarationName Name) {
  llvm::SmallVector<clang::NamedDecl *, 4> decls;
  // SetExternalVisibleDeclsForName() should make all declarations visible
  // that are from an AST file.
  for (auto *d : DC->noload_decls())
    if (clang::NamedDecl *nd = llvm::dyn_cast<clang::NamedDecl>(d)) {
      if (nd->isFromASTFile() && nd->getDeclName() == Name)
        decls.push_back(nd);
    }
  return !SetExternalVisibleDeclsForName(DC, Name, decls).empty();
}

OptionalClangModuleID
ClangExternalASTSourceCallbacks::RegisterModule(clang::Module *module) {
  m_modules.push_back(module);
  unsigned id = m_modules.size();
  m_ids.insert({module, id});
  return OptionalClangModuleID(id);
}

llvm::Optional<clang::ASTSourceDescriptor>
ClangExternalASTSourceCallbacks::getSourceDescriptor(unsigned id) {
  if (clang::Module *module = getModule(id))
    return {*module};
  return {};
}

clang::Module *ClangExternalASTSourceCallbacks::getModule(unsigned id) {
  if (id && id <= m_modules.size())
    return m_modules[id - 1];
  return nullptr;
}

OptionalClangModuleID
ClangExternalASTSourceCallbacks::GetIDForModule(clang::Module *module) {
  return OptionalClangModuleID(m_ids[module]);
}
