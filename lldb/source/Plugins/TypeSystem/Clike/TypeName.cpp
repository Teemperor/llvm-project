//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeName.h"

#include "Type.h"

namespace lldb_private {
using namespace lldb_private::clike;

std::string clike::BuildDisplayName(Type *t, bool keep_inline_namespaces) {
  if (!t)
    return "";
  DisplayNameOptions opts;
  opts.keep_inline_namespaces = keep_inline_namespaces;
  return t->GetDisplayName(opts);
}

std::string clike::BuildCanonicalName(Type *t, bool base_only) {
  if (!t)
    return std::string();
  return t->GetCanonicalName(base_only);
}

std::string clike::Type::GetDisplayName(const DisplayNameOptions &opts) const {
  // A named type. Builtins carry no unqualified name; fall back to their
  // stored name.
  llvm::StringRef unqualified = GetUnqualifiedName().GetName();
  if (unqualified.empty())
    return GetName().GetName().str();
  return unqualified.str();
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

} // namespace lldb_private
