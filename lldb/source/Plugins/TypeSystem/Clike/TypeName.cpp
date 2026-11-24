//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeName.h"

#include "FunctionScope.h"
#include "Namespace.h"
#include "Type.h"
#include "TypeC.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/FormatVariadic.h"

#include <cstdio>
#include <optional>

namespace lldb_private {
// NB: everything below sits inside lldb_private rather than pulling it in with
// a using-directive. lldb_private declares an unrelated lldb_private::Type, and
// importing both namespaces at file scope would make every bare `Type` here
// ambiguous with the clike one this file is all about; inside lldb_private the
// inner clike::Type wins.
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
// spelling that data formatters key on. Used when reconstructing a name from
// the modeled template arguments (see Type::NeedsTemplateNameReconstruction)
// instead of taking the DWARF spelling verbatim.
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

// The namespace enclosing all the scopes that qualify the name of `t` (e.g.
// `ns` for `ns::Outer::Inner` and for `ns::func()::Local::Nested`). A type
// declared directly in a function has no scopes in its name at all (`Local`,
// not `func()::Local`), as clang's printNestedNameSpecifier stops at a
// function, so this is null for it.
static const clike::Namespace *GetNameEnclosingNamespace(const Type &t) {
  const DeclarationContext *ctx = t.GetDeclContext();
  if (ctx && llvm::isa<FunctionScope>(ctx))
    return nullptr;
  return GetEnclosingNamespace(ctx);
}

// Append the class and function scopes in the declaration context chain
// `ctx`, up to its innermost enclosing namespace, outermost first and each
// followed by "::" (e.g. `Outer::` or `func()::Local::`). These are spelled
// like in the qualified DWARF name of a named type: a record by its
// unqualified name (or e.g. "(unnamed struct)") and a function by its name and
// parameter types.
static void AppendClassAndFunctionScopes(const DeclarationContext *ctx,
                                         std::string &out) {
  if (!ctx || llvm::isa<Namespace>(ctx))
    return;
  AppendClassAndFunctionScopes(ctx->GetParent(), out);
  if (const auto *record = llvm::dyn_cast<RecordType>(ctx)) {
    llvm::StringRef name = record->GetUnqualifiedName().GetName();
    if (!name.empty())
      out += name.str();
    else if (record->IsUnion())
      out += "(unnamed union)";
    else if (record->IsClassKeyword())
      out += "(unnamed class)";
    else
      out += "(unnamed struct)";
  } else if (const auto *function = llvm::dyn_cast<FunctionScope>(ctx)) {
    out += function->GetName().GetName().str();
  }
  out += "::";
}

// Count the enclosing namespaces of `ns` (including inline ones), i.e. how many
// leading scope components of a qualified name are namespaces rather than
// enclosing classes.
static unsigned CountNamespaceDepth(const clike::Namespace *ns) {
  unsigned depth = 0;
  for (; ns; ns = ns->GetParent())
    ++depth;
  return depth;
}

// Drop the leading components of the qualified spelling `name` that
// correspond to the namespace `ns` and its parents (plus a leading "::"
// global-scope marker), as the caller emits those itself (e.g. with inline
// namespaces hidden). Namespace names (including "(anonymous namespace)")
// never contain "::". Returns std::nullopt if `name` has fewer components.
static std::optional<llvm::StringRef>
DropNamespaceScopes(llvm::StringRef name, const clike::Namespace *ns) {
  name.consume_front("::");
  for (unsigned ns_depth = CountNamespaceDepth(ns); ns_depth; --ns_depth) {
    size_t sep = name.find("::");
    if (sep == llvm::StringRef::npos)
      return std::nullopt;
    name = name.drop_front(sep + 2);
  }
  return name;
}

// For a type nested inside a class or a function-local class (e.g.
// `Foo<int>::Nested<char>` or `ns::func(int)::Local::Nested`), the namespaces
// enclosing those scopes are rendered from the Namespace chain (see
// GetNameEnclosingNamespace), but the class and function scopes are taken
// verbatim from the type's fully-qualified DWARF spelling: drop the leading
// components that correspond to the namespaces and the trailing base name, and
// append what remains (e.g. `Foo<int>::` or `func(int)::Local::`).
static void AppendClassScopePrefix(llvm::StringRef qualified_name,
                                   const clike::Namespace *ns,
                                   std::string &out) {
  std::optional<llvm::StringRef> name = DropNamespaceScopes(qualified_name, ns);
  if (!name)
    return;
  // Split off the base name at the last "::" that is not nested in template
  // arguments or a parameter list (e.g. the one in `Tmpl<ns::X>` or
  // `method(ns::X)`). Scanning backwards keeps this robust against scopes
  // that contain operator names such as `operator<`.
  unsigned depth = 0;
  for (size_t i = name->size(); i-- > 1;) {
    char c = (*name)[i];
    if (c == '>' || c == ')')
      ++depth;
    else if ((c == '<' || c == '(') && depth)
      --depth;
    else if (c == ':' && (*name)[i - 1] == ':' && depth == 0) {
      out += name->take_front(i + 1).str();
      return;
    }
  }
}

// Append a pointer/reference declarator `sigil` to the name of the type it
// applies to.
static std::string AppendDeclaratorSigil(std::string name,
                                         llvm::StringRef sigil) {
  // Clang omits the space between a pointer/reference sigil and a following
  // '*' (e.g. "int **", "void **", "int &*"), but keeps it after a plain type
  // name ("int *").
  const bool tight =
      !name.empty() && (name.back() == '*' || name.back() == '&');
  return name + (tight ? "" : " ") + sigil.str();
}

// Render a character constant as a C++ character literal (e.g. `'v'`),
// escaping non-printable characters like clang's CharacterLiteral printer.
static std::string BuildCharLiteral(unsigned char c) {
  std::string out = "'";
  switch (c) {
  case '\\':
    out += "\\\\";
    break;
  case '\'':
    out += "\\'";
    break;
  case '\a':
    out += "\\a";
    break;
  case '\b':
    out += "\\b";
    break;
  case '\f':
    out += "\\f";
    break;
  case '\n':
    out += "\\n";
    break;
  case '\r':
    out += "\\r";
    break;
  case '\t':
    out += "\\t";
    break;
  case '\v':
    out += "\\v";
    break;
  default:
    if (c >= 0x20 && c <= 0x7e) {
      out += static_cast<char>(c);
    } else {
      // Print as a hex escape, matching clang's `\xNN` form.
      out += llvm::formatv("\\x{0:x-}", c).str();
    }
    break;
  }
  out += "'";
  return out;
}

// Render a floating-point non-type template argument from its raw value bits,
// matching clang's scientific `%e` formatting (e.g. `2.000000e+00`). The
// argument type's byte size selects the IEEE semantics.
static std::string BuildFloatArgName(uint64_t value,
                                     const clike::Type &value_type) {
  std::optional<uint64_t> byte_size = value_type.GetByteSize();
  // Decode the raw bits into a double, then format with C's `%e`.
  double d = 0.0;
  if (byte_size == 8) {
    d = llvm::bit_cast<double>(value);
  } else if (byte_size == 4) {
    d = llvm::bit_cast<float>(static_cast<uint32_t>(value));
  } else if (byte_size == 2) {
    // Half (__fp16/_Float16) and bfloat (__bf16) share a 16-bit width but use
    // different semantics; distinguish by the type's spelling.
    const llvm::fltSemantics &sem =
        value_type.GetName().GetName().contains("bf16")
            ? llvm::APFloat::BFloat()
            : llvm::APFloat::IEEEhalf();
    llvm::APFloat f(sem, llvm::APInt(16, value));
    d = f.convertToDouble();
  } else {
    return std::to_string(value);
  }
  char buf[64];
  ::snprintf(buf, sizeof(buf), "%e", d);
  return buf;
}

} // namespace

std::string clike::BuildDisplayName(Type *t, bool hide_default_args,
                                    bool keep_inline_namespaces) {
  if (!t)
    return "";
  DisplayNameOptions opts;
  opts.hide_default_args = hide_default_args;
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
  const clike::Namespace *ns = GetNameEnclosingNamespace(t);
  if (keep_inline_namespaces)
    AppendQualifiedNamespacePrefix(ns, result);
  else
    AppendNamespacePrefix(ns, result);
  AppendClassScopePrefix(t.GetName().GetName(), ns, result);
  return result;
}

std::string clike::BuildUnnamedTagScopePrefix(const Type &t,
                                              bool keep_inline_namespaces) {
  std::string result;
  // A type declared directly in a function has no scopes in its name (see
  // GetNameEnclosingNamespace).
  const DeclarationContext *ctx = t.GetDeclContext();
  if (ctx && llvm::isa<FunctionScope>(ctx))
    return result;
  AppendUnnamedTagNamespacePrefix(GetNameEnclosingNamespace(t), result,
                                  keep_inline_namespaces);
  AppendClassAndFunctionScopes(ctx, result);
  return result;
}

std::string clike::Type::GetDisplayName(const DisplayNameOptions &opts) const {
  // A named type (e.g. a typedef), qualified with its enclosing scopes.
  // Builtins carry no unqualified name; fall back to their stored name.
  llvm::StringRef unqualified = GetUnqualifiedName().GetName();
  if (unqualified.empty())
    return GetName().GetName().str();
  return BuildScopePrefix(*this, opts.keep_inline_namespaces) +
         unqualified.str();
}

std::string clike::Type::GetCanonicalName(bool base_only) const {
  // A named type (e.g. a typedef). `base_only` asks for the unqualified
  // spelling (no enclosing scopes), matching clang's
  // GetTypeNameForDecl(qualified=false); this is what
  // SymbolFileDWARF::FindTypes compares against a template query's basename
  // (e.g. "Nested<char>", not the scoped "Foo<int>::Nested<char>"). Builtins
  // carry no unqualified name, so fall back to the full name for them.
  //
  // The name comes verbatim from the debug info -- data formatters (e.g.
  // libc++'s) match against exactly that spelling, so it must be preserved.
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

std::string
clike::Type::GetTemplateArgValueName(uint64_t value,
                                     const DisplayNameOptions &opts) const {
  // A `char`-family argument prints as a character literal ('v'), a `bool`
  // argument prints as true/false, and a floating-point argument prints in
  // scientific form.
  switch (GetFormat()) {
  case lldb::eFormatChar:
    return BuildCharLiteral(static_cast<unsigned char>(value));
  case lldb::eFormatBoolean:
    return value ? "true" : "false";
  default:
    break;
  }
  if (GetEncoding() == lldb::eEncodingIEEE754)
    return BuildFloatArgName(value, *this);
  // Other integral arguments: print the numeric value, honoring signedness.
  if (GetEncoding() == lldb::eEncodingSint)
    return std::to_string(static_cast<int64_t>(value));
  return std::to_string(value);
}

} // namespace lldb_private
