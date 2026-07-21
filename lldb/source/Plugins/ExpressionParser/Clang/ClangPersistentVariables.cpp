//===-- ClangPersistentVariables.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "ClangPersistentVariables.h"
#include "ClangASTImporter.h"
#include "ClangModulesDeclVendor.h"

#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "lldb/Core/Value.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Log.h"
#include "lldb/Utility/StreamString.h"

#include "clang/AST/Decl.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringSet.h"
#include <optional>
#include <memory>

using namespace lldb;
using namespace lldb_private;

char ClangPersistentVariables::ID;

ClangPersistentVariables::ClangPersistentVariables(
    std::shared_ptr<Target> target_sp)
    : m_target_sp(target_sp) {}

ExpressionVariableSP ClangPersistentVariables::CreatePersistentVariable(
    const lldb::ValueObjectSP &valobj_sp) {
  return AddNewlyConstructedVariable(new ClangExpressionVariable(valobj_sp));
}

ExpressionVariableSP ClangPersistentVariables::CreatePersistentVariable(
    ExecutionContextScope *exe_scope, ConstString name,
    const CompilerType &compiler_type, lldb::ByteOrder byte_order,
    uint32_t addr_byte_size) {
  return AddNewlyConstructedVariable(new ClangExpressionVariable(
      exe_scope, name, compiler_type, byte_order, addr_byte_size));
}

void ClangPersistentVariables::RemovePersistentVariable(
    lldb::ExpressionVariableSP variable) {
  RemoveVariable(variable);

  // Check if the removed variable was the last one that was created. If yes,
  // reuse the variable id for the next variable.

  // Nothing to do if we have not assigned a variable id so far.
  if (m_next_persistent_variable_id == 0)
    return;

  llvm::StringRef name = variable->GetName().GetStringRef();
  // Remove the prefix from the variable that only the indes is left.
  if (!name.consume_front(GetPersistentVariablePrefix(false)))
    return;

  // Check if the variable contained a variable id.
  uint32_t variable_id;
  if (name.getAsInteger(10, variable_id))
    return;
  // If it's the most recent variable id that was assigned, make sure that this
  // variable id will be used for the next persistent variable.
  if (variable_id == m_next_persistent_variable_id - 1)
    m_next_persistent_variable_id--;
}

std::optional<CompilerType>
ClangPersistentVariables::GetCompilerTypeFromPersistentDecl(
    ConstString type_name) {
  // A TypeSystemCpp-backed persistent type (registered by
  // RegisterPersistentType) takes priority: it's a plain CompilerType lookup,
  // no clang::NamedDecl/ASTContext involved.
  auto type_it = m_persistent_types.find(type_name.GetCString());
  if (type_it != m_persistent_types.end())
    return type_it->second;

  PersistentDecl p = m_persistent_decls.lookup(type_name.GetCString());

  if (p.m_decl == nullptr)
    return std::nullopt;

  auto ctx = std::static_pointer_cast<TypeSystemClang>(p.m_context.lock());
  if (clang::TypeDecl *tdecl = llvm::dyn_cast<clang::TypeDecl>(p.m_decl)) {
    opaque_compiler_type_t t =
        static_cast<opaque_compiler_type_t>(const_cast<clang::Type *>(
            ctx->getASTContext().getTypeDeclType(tdecl).getTypePtr()));
    return CompilerType(p.m_context, t);
  }
  return std::nullopt;
}

void ClangPersistentVariables::RegisterPersistentType(ConstString name,
                                                      CompilerType type) {
  m_persistent_types[name.GetCString()] = type;
}

/// Collect the set of C identifier tokens (letters/digits/underscore, and `$`
/// so persistent names like `$foo` are recognized) appearing in \p text.
static void CollectIdentifierTokens(llvm::StringRef text,
                                    llvm::StringSet<> &tokens) {
  auto is_ident_start = [](char c) {
    return llvm::isAlpha(c) || c == '_' || c == '$';
  };
  auto is_ident_body = [](char c) {
    return llvm::isAlnum(c) || c == '_' || c == '$';
  };
  for (size_t i = 0, n = text.size(); i < n;) {
    if (!is_ident_start(text[i])) {
      ++i;
      continue;
    }
    size_t start = i;
    while (i < n && is_ident_body(text[i]))
      ++i;
    tokens.insert(text.substr(start, i - start));
  }
}

void ClangPersistentVariables::RegisterTopLevelSource(
    std::vector<std::string> names, std::string source) {
  if (names.empty() || source.empty())
    return;
  m_top_level_sources.push_back({std::move(names), std::move(source)});
}

std::string ClangPersistentVariables::GetInjectedTopLevelSource(
    llvm::StringRef expr_text) const {
  if (m_top_level_sources.empty())
    return {};

  // The set of names the (growing) expression refers to. We seed it from the
  // expression itself and then, each time we pull in a top-level source,
  // fold in the identifiers *it* references so a chain of top-level functions
  // calling each other is injected transitively.
  llvm::StringSet<> wanted;
  CollectIdentifierTokens(expr_text, wanted);

  std::vector<bool> included(m_top_level_sources.size(), false);
  bool changed = true;
  while (changed) {
    changed = false;
    for (size_t i = 0; i < m_top_level_sources.size(); ++i) {
      if (included[i])
        continue;
      const TopLevelSource &tls = m_top_level_sources[i];
      bool referenced = false;
      for (const std::string &name : tls.names) {
        if (wanted.contains(name)) {
          referenced = true;
          break;
        }
      }
      if (!referenced)
        continue;
      included[i] = true;
      changed = true;
      CollectIdentifierTokens(tls.source, wanted);
    }
  }

  // Emit in declaration order so an earlier top-level decl is visible to a
  // later one that uses it (matters for C).
  std::string result;
  for (size_t i = 0; i < m_top_level_sources.size(); ++i) {
    if (!included[i])
      continue;
    result += m_top_level_sources[i].source;
    result += '\n';
  }
  return result;
}

void ClangPersistentVariables::RegisterPersistentDecl(
    ConstString name, clang::NamedDecl *decl,
    std::shared_ptr<TypeSystemClang> ctx) {
  PersistentDecl p = {decl, ctx};
  m_persistent_decls.insert(std::make_pair(name.GetCString(), p));

  if (clang::EnumDecl *enum_decl = llvm::dyn_cast<clang::EnumDecl>(decl)) {
    for (clang::EnumConstantDecl *enumerator_decl : enum_decl->enumerators()) {
      p = {enumerator_decl, ctx};
      m_persistent_decls.insert(std::make_pair(
          ConstString(enumerator_decl->getNameAsString()).GetCString(), p));
    }
  }
}

clang::NamedDecl *
ClangPersistentVariables::GetPersistentDecl(ConstString name) {
  return m_persistent_decls.lookup(name.GetCString()).m_decl;
}

std::shared_ptr<ClangASTImporter>
ClangPersistentVariables::GetClangASTImporter() {
  if (!m_ast_importer_sp) {
    m_ast_importer_sp = std::make_shared<ClangASTImporter>();
  }
  return m_ast_importer_sp;
}

std::shared_ptr<ClangModulesDeclVendor>
ClangPersistentVariables::GetClangModulesDeclVendor() {
  if (!m_modules_decl_vendor_sp) {
    m_modules_decl_vendor_sp.reset(
        ClangModulesDeclVendor::Create(*m_target_sp));
  }
  return m_modules_decl_vendor_sp;
}

ConstString
ClangPersistentVariables::GetNextPersistentVariableName(bool is_error) {
  llvm::SmallString<64> name;
  {
    llvm::raw_svector_ostream os(name);
    os << GetPersistentVariablePrefix(is_error)
       << m_next_persistent_variable_id++;
  }
  return ConstString(name);
}
