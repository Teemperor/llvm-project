//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeName.h"

#include "BuiltinTypes.h"
#include "Type.h"
#include "TypeC.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"

namespace lldb_private {
using namespace lldb_private::clike;

namespace {

// Build a type's (simplified) display name from the type model: qualified with
// its declaring namespaces (inline ones skipped).
static std::string BuildDisplayNameImpl(clike::Type *t,
                                        bool keep_inline_namespaces) {
  using namespace clike;
  if (!t)
    return "";

  // Named leaf type (record/typedef/enum/builtin). Builtins carry no
  // unqualified name; fall back to their stored name.
  llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
  if (unqualified.empty()) {
    // Builtins have no unqualified name but do carry a stored name.
    return t->GetName().GetName().str();
  }

  return unqualified.str();
}

} // namespace

// The exported entry points. BuildDisplayName just names the file-local
// implementation above; the recursion stays internal.

std::string clike::BuildDisplayName(Type *t, bool keep_inline_namespaces) {
  return BuildDisplayNameImpl(t, keep_inline_namespaces);
}

std::string clike::BuildCanonicalName(Type *t, bool base_only) {
  if (!t)
    return std::string();
  // Named leaf type (record/typedef/enum/builtin). `base_only` asks for the
  // unqualified spelling (no enclosing scopes), matching clang's
  // GetTypeNameForDecl(qualified=false). Builtins carry no unqualified name, so
  // fall back to the full name for them.
  //
  // The name comes verbatim from the debug info -- data formatters match
  // against exactly that spelling, so it must be preserved.
  if (base_only) {
    llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
    if (!unqualified.empty())
      return unqualified.str();
  }
  return t->GetName().GetName().str();
}

} // namespace lldb_private
