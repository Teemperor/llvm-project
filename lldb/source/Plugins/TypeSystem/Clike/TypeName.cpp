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
  // Composite types have no name of their own; build them from their parts.
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    clike::Type *pointee = ptr->GetPointeeType();
    // `void *` needs no special case: its pointee is the `void` builtin, whose
    // name is "void".
    std::string pointee_name =
        BuildDisplayNameImpl(pointee, keep_inline_namespaces);
    // Clang omits the space between a pointer sigil and a following '*' (e.g.
    // "int **", "void **"), but keeps it after a plain type name ("int *").
    const bool tight = !pointee_name.empty() && pointee_name.back() == '*';
    return pointee_name + (tight ? "*" : " *");
  }
  if (auto *cv = llvm::dyn_cast<CVQualifiedType>(t)) {
    std::string qualifier;
    if (cv->IsConst())
      qualifier += "const";
    if (cv->IsVolatile())
      qualifier += qualifier.empty() ? "volatile" : " volatile";
    std::string underlying =
        BuildDisplayNameImpl(cv->GetUnderlyingType(), keep_inline_namespaces);
    // A cv-qualifier on a pointer itself is a declarator suffix (`T *const`),
    // not a prefix (`const T *`, which would instead qualify the pointee) --
    // matching the PtrAuthType case below and clang's own type printer. This
    // only applies to the pointer directly under this qualifier: a qualified
    // pointee (`const T *const`) has already rendered its own leading `const`
    // as part of `underlying` (via the pointee's own CVQualifiedType), so no
    // double-prefixing happens here.
    if (llvm::isa<PointerType>(cv->GetUnderlyingType())) {
      // No space between the `*` and the qualifier (`T *const`), matching
      // clang's declarator-suffix spelling.
      const bool tight = !underlying.empty() && underlying.back() == '*';
      return underlying + (tight ? "" : " ") + qualifier;
    }
    return qualifier + (qualifier.empty() ? "" : " ") + underlying;
  }

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
  // Pointers have no name of their own either; build "<pointee> *".
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    Type *pointee = ptr->GetPointeeType();
    std::string pointee_name = BuildCanonicalName(pointee, base_only);
    // Clang omits the space before '*' when the pointee already ends in a
    // pointer sigil ("void **"), so keep the two tight.
    const bool tight = !pointee_name.empty() && pointee_name.back() == '*';
    return pointee_name + (tight ? "*" : " *");
  }
  // cv-qualified types render as "const"/"volatile" prefixing the unqualified
  // name. Matching TypeSystemClang's GetTypeName: the cv-qualifiers are only
  // spelled for types whose name is printed from the QualType (builtins,
  // pointers, ...). For a tag type (record/enum) or a typedef the
  // name is taken from the underlying decl, which carries no qualifiers, so
  // `const Enum` prints as `Enum` (while `const int` stays `const int`).
  if (auto *cv = llvm::dyn_cast<CVQualifiedType>(t)) {
    std::string underlying_name =
        BuildCanonicalName(cv->GetUnderlyingType(), base_only);
    // Look through display/cv sugar to the leaf type to decide whether the
    // qualifiers are spelled.
    Type *leaf = cv->GetUnderlyingType();
    while (true) {
      if (auto *inner = llvm::dyn_cast<CVQualifiedType>(leaf))
        leaf = inner->GetUnderlyingType();
      else
        break;
    }
    const bool drop_qualifiers = llvm::isa<TypedefType>(leaf);
    if (drop_qualifiers)
      return underlying_name;
    std::string result;
    if (cv->IsConst())
      result += "const ";
    if (cv->IsVolatile())
      result += "volatile ";
    result += underlying_name;
    return result;
  }
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
