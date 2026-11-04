//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeName.h"

#include "Namespace.h"
#include "Type.h"
#include "TypeC.h"

#include "llvm/Support/Casting.h"

namespace lldb_private {
using namespace lldb_private::clike;

namespace {

// Append the namespace qualification for `ns` (outermost first), skipping
// inline namespaces so that e.g. `std::__1` prints as `std::`, and anonymous
// namespaces so that a type in one prints unqualified (e.g. `Bar`, matching
// clang, which elides its `(anonymous namespace)` scope).
static void AppendNamespacePrefix(const clike::Namespace *ns,
                                  std::string &out) {
  if (!ns)
    return;
  AppendNamespacePrefix(ns->GetParent(), out);
  if (!ns->IsInline() && !ns->IsAnonymous()) {
    out += ns->GetName().GetName().str();
    out += "::";
  }
}

// Like AppendNamespacePrefix, but for the fully-qualified (non-display) name:
// keeps inline namespaces (e.g. `std::__1::`), matching the raw DWARF
// spelling that data formatters key on.
static void AppendQualifiedNamespacePrefix(const clike::Namespace *ns,
                                           std::string &out) {
  if (!ns)
    return;
  AppendQualifiedNamespacePrefix(ns->GetParent(), out);
  if (!ns->IsAnonymous()) {
    out += ns->GetName().GetName().str();
    out += "::";
  }
}

// Like AppendNamespacePrefix, but for an *unnamed* tag type (e.g. a lambda
// closure), which has no name of its own to make an elided anonymous
// namespace unambiguous. Clang's printer elides `(anonymous namespace)` when
// qualifying a *named* entity (nothing else identifies the entity within its
// TU, so the extra scope is noise) but keeps the literal "(anonymous
// namespace)" marker when qualifying an unnamed one printed as e.g.
// "(unnamed class)" -- otherwise the qualification would be dropped entirely.
// `keep_inline_namespaces` has the same meaning as for
// AppendNamespacePrefix/AppendQualifiedNamespacePrefix.
static void AppendUnnamedTagNamespacePrefix(const clike::Namespace *ns,
                                            std::string &out,
                                            bool keep_inline_namespaces) {
  if (!ns)
    return;
  AppendUnnamedTagNamespacePrefix(ns->GetParent(), out, keep_inline_namespaces);
  if (ns->IsInline() && !keep_inline_namespaces)
    return;
  if (ns->IsAnonymous())
    out += "(anonymous namespace)::";
  else {
    out += ns->GetName().GetName().str();
    out += "::";
  }
}

// Append a pointer declarator `sigil` to the name of the type it applies to.
static std::string AppendDeclaratorSigil(std::string name,
                                         llvm::StringRef sigil) {
  // Clang omits the space between a pointer sigil and a following '*' (e.g.
  // "int **", "void **"), but keeps it after a plain type name ("int *").
  const bool tight = !name.empty() && name.back() == '*';
  return name + (tight ? "" : " ") + sigil.str();
}

} // namespace

std::string clike::BuildDisplayName(Type *t, bool keep_inline_namespaces) {
  if (!t)
    return "";
  DisplayNameOptions opts;
  opts.keep_inline_namespaces = keep_inline_namespaces;
  return t->GetDisplayName(opts);
}

std::string clike::BuildFunctionName(Type *fn, llvm::StringRef decl,
                                     bool keep_inline_namespaces) {
  return llvm::cast<FunctionType>(fn)->GetDeclaratorName(
      decl, keep_inline_namespaces);
}

std::string clike::BuildCanonicalName(Type *t, bool base_only) {
  if (!t)
    return std::string();
  return t->GetCanonicalName(base_only);
}

std::string clike::BuildScopePrefix(const Type &t,
                                    bool keep_inline_namespaces) {
  std::string result;
  const clike::Namespace *ns =
      GetEnclosingNamespace(&t.GetDeclContext(t.GetOwningContext()));
  if (keep_inline_namespaces)
    AppendQualifiedNamespacePrefix(ns, result);
  else
    AppendNamespacePrefix(ns, result);
  return result;
}

std::string clike::BuildUnnamedTagScopePrefix(const Type &t,
                                              bool keep_inline_namespaces) {
  std::string result;
  AppendUnnamedTagNamespacePrefix(
      GetEnclosingNamespace(&t.GetDeclContext(t.GetOwningContext())), result,
      keep_inline_namespaces);
  return result;
}

std::string clike::Type::GetDisplayName(const DisplayNameOptions &opts) const {
  // A named type, qualified with its enclosing scopes. Builtins carry no
  // unqualified name; fall back to their stored name.
  llvm::StringRef unqualified = GetUnqualifiedName().GetName();
  if (unqualified.empty())
    return GetName().GetName().str();
  return BuildScopePrefix(*this, opts.keep_inline_namespaces) +
         unqualified.str();
}

std::string clike::Type::GetCanonicalName(bool base_only) const {
  // A named type. `base_only` asks for the unqualified spelling (no enclosing
  // scopes), matching clang's GetTypeNameForDecl(qualified=false). Builtins
  // carry no unqualified name, so fall back to the full name for them.
  //
  // The name comes verbatim from the debug info -- data formatters match
  // against exactly that spelling, so it must be preserved.
  if (base_only) {
    llvm::StringRef unqualified = GetUnqualifiedName().GetName();
    if (!unqualified.empty())
      return unqualified.str();
  }
  return GetName().GetName().str();
}

std::string
clike::Type::GetPointeeDisplayName(llvm::StringRef sigil,
                                   const DisplayNameOptions &opts) const {
  // `void *` needs no special case: its pointee is the `void` builtin, whose
  // name is "void".
  return AppendDeclaratorSigil(GetDisplayName(opts), sigil);
}

std::string clike::Type::GetPointeeCanonicalName(llvm::StringRef sigil,
                                                 bool base_only) const {
  return AppendDeclaratorSigil(GetCanonicalName(base_only), sigil);
}

} // namespace lldb_private
