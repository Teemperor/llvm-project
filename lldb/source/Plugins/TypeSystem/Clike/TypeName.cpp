//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeName.h"

#include "BuiltinTypes.h"
#include "Namespace.h"
#include "Type.h"
#include "TypeC.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"

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

// Like AppendNamespacePrefix, but for an *unnamed* tag type, which has no name
// of its own to make an elided anonymous namespace unambiguous. Clang's printer
// elides `(anonymous namespace)` when qualifying a *named* entity (nothing else
// identifies the entity within its TU, so the extra scope is noise) but keeps
// the literal "(anonymous namespace)" marker when qualifying an unnamed one
// printed as e.g. "(unnamed class)" -- otherwise the qualification would be
// dropped entirely.
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

// An unnamed record/enum (e.g. a function-local `struct { ... } x;`) has no
// spelling in the debug info. Match clang's TagDecl printing, which renders
// such a type as "(unnamed struct)" / "(unnamed union)" / "(unnamed class)" /
// "(unnamed enum)" (see TagDecl::printName) -- except an *anonymous*
// struct/union (RecordType::IsAnonymousStructOrUnion, e.g. `struct { int x; };`
// with no member name) prints "(anonymous struct)"/"(anonymous union)" instead,
// matching clang's TagDecl::printAnonymousTagDecl, which special-cases
// RecordDecl::isAnonymousStructOrUnion() to say "anonymous" rather than
// "unnamed". Returns an empty string for any other (named or non-tag) type.
static std::string BuildUnnamedTagName(clike::Type *t) {
  using namespace clike;
  if (!t->GetName().GetName().empty() ||
      !t->GetUnqualifiedName().GetName().empty())
    return {};
  if (auto *rec = llvm::dyn_cast<RecordType>(t)) {
    std::string adj = rec->IsAnonymousStructOrUnion() ? "anonymous" : "unnamed";
    if (rec->IsUnion())
      return "(" + adj + " union)";
    if (rec->IsClassKeyword())
      return "(" + adj + " class)";
    return "(" + adj + " struct)";
  }
  if (llvm::isa<EnumType>(t))
    return "(unnamed enum)";
  return {};
}

static std::string BuildDisplayNameImpl(clike::Type *t,
                                        bool keep_inline_namespaces = false);

// An anonymous struct/union (RecordType::IsAnonymousStructOrUnion) has no name
// of its own to qualify -- its DeclContext (which only models enclosing
// *namespaces*, never enclosing classes) is therefore useless for recovering
// its enclosing scope. Instead it carries a direct pointer to the record it is
// embedded in (GetAnonymousParent()); use that record's own (already fully
// qualified) display name as the prefix, e.g. "MySock::" for the union in
// `struct MySock { union { ... }; };`. This mirrors clang's
// NamedDecl::printNestedNameSpecifier, which walks the DeclContext chain and,
// unlike this model, treats an enclosing RecordDecl as a DeclContext too. Does
// nothing if `t` is not an anonymous struct/union (e.g. a function-local
// unnamed struct, which has no anonymous parent and clang likewise never
// prefixes: printNestedNameSpecifier returns early for a function DeclContext).
static void AppendAnonymousParentPrefix(clike::Type *t, std::string &out) {
  auto *rec = llvm::dyn_cast<clike::RecordType>(t);
  if (!rec)
    return;
  const clike::RecordType *parent = rec->GetAnonymousParent();
  if (!parent)
    return;
  out += BuildDisplayNameImpl(const_cast<clike::RecordType *>(parent));
  out += "::";
}

// Render an ARMv8.3 pointer-auth qualifier as `__ptrauth(key,addr_disc,extra)`,
// matching clang's TypePrinter / TypeSystemClang spelling.
static std::string BuildPtrAuthQualifier(const clike::PtrAuthType *pa) {
  return llvm::formatv("__ptrauth({0},{1},{2})", pa->GetKey(),
                       pa->IsAddressDiscriminated() ? 1 : 0,
                       pa->GetExtraDiscriminator())
      .str();
}

// Render a function signature in C declarator form, placing `decl` (e.g. "" for
// a plain function, "(*)" for a function pointer, "(^)" for a block pointer)
// between the return type and the parameter list: `int (*)(const char *)`.
static std::string BuildFunctionNameImpl(clike::FunctionType *fn,
                                         llvm::StringRef decl,
                                         bool keep_inline_namespaces = false) {
  // A function that returns nothing returns the `void` builtin, so there is
  // always a return type to name.
  std::string ret =
      BuildDisplayNameImpl(fn->GetReturnType(), keep_inline_namespaces);
  std::string params;
  for (uint32_t i = 0, e = fn->GetNumParameters(); i != e; ++i) {
    if (!params.empty())
      params += ", ";
    params += BuildDisplayNameImpl(fn->GetParameterAtIndex(i),
                                   keep_inline_namespaces);
  }
  if (fn->IsVariadic())
    params += params.empty() ? "..." : ", ...";
  // An empty, prototyped parameter list is rendered as `()` in C++ (matching
  // clang's TypePrinter with UseVoidForZeroParams=false) but as `(void)` in C,
  // where an empty parameter list without `void` instead means "unspecified
  // parameters" (K&R). See FunctionType::UseVoidForEmptyParams for how that bit
  // is determined at parse time. An unprototyped (K&R) function has a
  // DW_TAG_unspecified_parameters child and is modelled as variadic above, so
  // it prints as `(...)` regardless.
  else if (params.empty() && fn->UseVoidForEmptyParams())
    params = "void";
  return ret + " " + decl.str() + "(" + params + ")";
}

// Build a type's (simplified) display name from the type model: qualified with
// its declaring namespaces (inline ones skipped).
static std::string BuildDisplayNameImpl(clike::Type *t,
                                        bool keep_inline_namespaces) {
  using namespace clike;
  if (!t)
    return "";
  // Composite types have no name of their own; build them from their parts.
  if (llvm::isa<ArrayType>(t)) {
    // A vector type (DW_AT_GNU_vector) is spelled like clang's ext_vector
    // attribute rather than as an array, so vector formatters match and
    // array/char-array formatters don't (see GetTypeName).
    if (auto *vec = llvm::dyn_cast<ArrayType>(t); vec && vec->IsVector()) {
      uint64_t n = vec->GetNumElements().value_or(0);
      return llvm::formatv("{0} __attribute__((ext_vector_type({1})))",
                           BuildDisplayNameImpl(vec->GetElementType(),
                                                keep_inline_namespaces),
                           n)
          .str();
    }
    // For a multidimensional C array the DWARF/type nesting goes
    // outermost-dimension first (`T[2][3]` == array-of-2 of array-of-3 of T),
    // but the element name must be printed innermost-last. Peel the whole
    // array chain collecting dimensions left-to-right, then append them after
    // the innermost element's name so the brackets keep their source order.
    std::string dims;
    clike::Type *cur = t;
    while (auto *array = llvm::dyn_cast<ArrayType>(cur)) {
      if (std::optional<uint64_t> n = array->GetNumElements())
        dims += llvm::formatv("[{0}]", *n).str();
      else
        dims += "[]";
      cur = array->GetElementType();
    }
    return BuildDisplayNameImpl(cur, keep_inline_namespaces) + dims;
  }
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    clike::Type *pointee = ptr->GetPointeeType();
    if (auto *fn = llvm::dyn_cast<FunctionType>(pointee))
      return BuildFunctionNameImpl(
          fn, llvm::isa<BlockPointerType>(ptr) ? "(^)" : "(*)",
          keep_inline_namespaces);
    // `void *` needs no special case: its pointee is the `void` builtin, whose
    // name is "void".
    std::string pointee_name =
        BuildDisplayNameImpl(pointee, keep_inline_namespaces);
    // Clang omits the space between a pointer sigil and a following '*' (e.g.
    // "int **", "void **"), but keeps it after a plain type name ("int *").
    const bool tight = !pointee_name.empty() && pointee_name.back() == '*';
    return pointee_name + (tight ? "*" : " *");
  }
  if (auto *fn = llvm::dyn_cast<FunctionType>(t))
    return BuildFunctionNameImpl(fn, "", keep_inline_namespaces);
  if (auto *cx = llvm::dyn_cast<ComplexType>(t)) {
    return "_Complex " +
           BuildDisplayNameImpl(cx->GetElementType(), keep_inline_namespaces);
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
  if (auto *pa = llvm::dyn_cast<PtrAuthType>(t)) {
    std::string underlying =
        BuildDisplayNameImpl(pa->GetUnderlyingType(), keep_inline_namespaces);
    std::string qualifier = BuildPtrAuthQualifier(pa);
    // Clang spells the qualifier as a declarator suffix on a pointer
    // (`int *__ptrauth(...)`) but as a prefix otherwise (`__ptrauth(...)
    // intp`).
    if (llvm::isa<PointerType>(pa->GetUnderlyingType()))
      return underlying + qualifier;
    return qualifier + " " + underlying;
  }
  // Elaborated display sugar: show the source spelling (e.g. `::Struct`) rather
  // than the wrapped type's own name.
  if (auto *el = llvm::dyn_cast<ElaboratedType>(t))
    return el->GetSpelling().GetName().str();

  // Named leaf type (record/typedef/enum/builtin). Builtins carry no
  // unqualified name; fall back to their stored name.
  llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
  if (unqualified.empty()) {
    // An unnamed record/enum prints as "(unnamed struct)" etc. (matching
    // clang); builtins have no unqualified name but do carry a stored name. An
    // anonymous struct/union additionally gets its enclosing record's name
    // prefixed (e.g. "MySock::(anonymous union)"), since clang's own printer
    // qualifies it that way (see AppendAnonymousParentPrefix). Otherwise
    // qualify by the enclosing namespace chain instead, same as a named type.
    if (std::string unnamed = BuildUnnamedTagName(t); !unnamed.empty()) {
      std::string result;
      AppendAnonymousParentPrefix(t, result);
      if (result.empty())
        AppendUnnamedTagNamespacePrefix(t->GetDeclContext(), result,
                                        keep_inline_namespaces);
      return result + unnamed;
    }
    return t->GetName().GetName().str();
  }

  std::string result;
  if (keep_inline_namespaces)
    AppendQualifiedNamespacePrefix(t->GetDeclContext(), result);
  else
    AppendNamespacePrefix(t->GetDeclContext(), result);
  result += unqualified.str();
  return result;
}

} // namespace

// The exported entry points. BuildDisplayName/BuildFunctionName just name the
// file-local implementations above; the recursion between them all stays
// internal.

std::string clike::BuildDisplayName(Type *t, bool keep_inline_namespaces) {
  return BuildDisplayNameImpl(t, keep_inline_namespaces);
}

std::string clike::BuildFunctionName(Type *fn, llvm::StringRef decl,
                                     bool keep_inline_namespaces) {
  return BuildFunctionNameImpl(llvm::cast<FunctionType>(fn), decl,
                               keep_inline_namespaces);
}

std::string clike::BuildCanonicalName(Type *t, bool base_only) {
  if (!t)
    return std::string();
  // Strip elaborated display sugar (e.g. the `::` of `::Struct`) from the
  // canonical name: the source spelling only affects the *display* name, so a
  // formatter keyed on `Struct` still matches a `::Struct`-spelled value. Typedefs are kept (they are meaningful, distinct
  // names).
  t = t->StripElaborated();
  // Arrays have no name of their own; build "<element>[<count>]". For a
  // multidimensional array the nesting is outermost-dimension first, so peel
  // the whole chain and print the dimensions in source order after the
  // innermost element's name.
  if (llvm::isa<ArrayType>(t)) {
    // A vector type (DW_AT_GNU_vector) is spelled like clang's
    // `<element> __attribute__((ext_vector_type(N)))` rather than as an array
    // (`<element>[N]`), so it doesn't get matched by array/char-array
    // formatters (matching TypeSystemClang's type name for vectors).
    if (auto *vec = llvm::dyn_cast<ArrayType>(t); vec && vec->IsVector()) {
      std::string element_name =
          BuildCanonicalName(vec->GetElementType(), base_only);
      uint64_t n = vec->GetNumElements().value_or(0);
      return llvm::formatv("{0} __attribute__((ext_vector_type({1})))",
                           element_name, n)
          .str();
    }
    std::string dims;
    Type *cur = t;
    while (auto *array = llvm::dyn_cast<ArrayType>(cur)) {
      if (std::optional<uint64_t> num_elements = array->GetNumElements())
        dims += llvm::formatv("[{0}]", *num_elements).str();
      else
        dims += "[]";
      cur = array->GetElementType();
    }
    std::string element_name = BuildCanonicalName(cur, base_only);
    return element_name + dims;
  }
  // Function types and pointers to them need C declarator syntax
  // (`int (*)(const char *)`); BuildDisplayNameImpl already produces it.
  if (llvm::isa<FunctionType>(t))
    return BuildDisplayNameImpl(t);
  if (llvm::isa<ComplexType>(t))
    return BuildDisplayNameImpl(t);
  // Pointers have no name of their own either; build "<pointee> *".
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    Type *pointee = ptr->GetPointeeType();
    if (llvm::isa<FunctionType>(pointee))
      return BuildDisplayNameImpl(t);
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
      if (auto *el = llvm::dyn_cast<ElaboratedType>(leaf))
        leaf = el->GetUnderlyingType();
      else if (auto *inner = llvm::dyn_cast<CVQualifiedType>(leaf))
        leaf = inner->GetUnderlyingType();
      else
        break;
    }
    const bool drop_qualifiers = llvm::isa<RecordType>(leaf) ||
                                 llvm::isa<EnumType>(leaf) ||
                                 llvm::isa<TypedefType>(leaf);
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
  if (auto *pa = llvm::dyn_cast<PtrAuthType>(t)) {
    std::string underlying =
        BuildCanonicalName(pa->GetUnderlyingType(), base_only);
    std::string qualifier = BuildPtrAuthQualifier(pa);
    // Suffix on a pointer (`int *__ptrauth(...)`), prefix otherwise
    // (`__ptrauth(...) intp`), matching clang's spelling.
    if (llvm::isa<PointerType>(pa->GetUnderlyingType()))
      return underlying + qualifier;
    return qualifier + " " + underlying;
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
  // An unnamed record/enum has no spelling in the debug info; render it as
  // "(unnamed struct)" etc. to match clang / TypeSystemClang. An anonymous
  // struct/union additionally gets its enclosing record's name prefixed (e.g.
  // "MySock::(anonymous union)"); see AppendAnonymousParentPrefix. Otherwise
  // qualify by the enclosing namespace chain instead (keeping inline
  // namespaces, matching the raw-spelling convention this function otherwise
  // uses). `base_only` asks for the unqualified spelling, so skip both prefixes
  // in that case.
  if (std::string unnamed = BuildUnnamedTagName(t); !unnamed.empty()) {
    std::string result;
    if (!base_only) {
      AppendAnonymousParentPrefix(t, result);
      if (result.empty())
        AppendUnnamedTagNamespacePrefix(t->GetDeclContext(), result,
                                        /*keep_inline_namespaces=*/true);
    }
    return result + unnamed;
  }
  return t->GetName().GetName().str();
}

} // namespace lldb_private
