//===-- CppModuleHandler.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ExpressionParser/Clang/CppModuleHandler.h"

#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclBase.h"

using namespace lldb_private;
using namespace clang;

NamespaceDecl *
CppModuleHandler::FindImportedNamespace(const DeclContext *parent_ctx,
                                        llvm::StringRef name) {
  ASTContext &ast = parent_ctx->getParentASTContext();
  IdentifierInfo &ident = ast.Idents.get(name);
  DeclarationName decl_name(&ident);
  auto *mutable_ctx = const_cast<DeclContext *>(parent_ctx);

  // A real `@import`ed module's top-level decls (e.g. `std`) are not
  // necessarily materialized as concrete Decls the moment the `@import` is
  // processed -- like PCH/module deserialization generally, a namespace can
  // stay latent in the serialized module until something actually performs a
  // real lookup for its name, at which point the ASTReader (attached as an
  // external source) deserializes and registers it. If we only ever used
  // noload_lookup, a namespace generated *before* anything forces that
  // deserialization (e.g. materializing a debug-info local variable's type
  // that happens to live in `std`, ahead of the user's own `std::...` text)
  // would see nothing yet and synthesize its own `std`; the real one would
  // then surface later when the user's text is parsed, leaving two unrelated
  // `std` decls in the same DeclContext -- Sema reports the name as
  // ambiguous.
  //
  // A real lookup risks reentrancy: this DeclContext may be one
  // ClangASTGenerator/ClikeExpressionDeclMap gave external visible storage to,
  // and forcing a lookup on it calls back into ClikeExpressionDeclMap's own
  // FindExternalVisibleDeclsByName, corrupting whatever resolution is
  // currently in flight (observed with TestInlineNamespaceAlias). Only take
  // that risk when a C++ module import is actually possible for this parse
  // (`target.import-std-module`, see SetupImportStdModuleLangOpts): then the
  // ASTContext's external source is a SemaSourceWithPriorities that tries the
  // real module's ASTReader *first* and returns without ever reaching us,
  // so the reentrancy the ordinary (no-module) path exposes doesn't occur.
  if (ast.getLangOpts().Modules) {
    for (NamedDecl *decl : mutable_ctx->lookup(decl_name))
      if (auto *nsd = dyn_cast<NamespaceDecl>(decl))
        return nsd;
    return nullptr;
  }

  for (NamedDecl *decl : mutable_ctx->noload_lookup(decl_name))
    if (auto *nsd = dyn_cast<NamespaceDecl>(decl))
      return nsd;
  return nullptr;
}
