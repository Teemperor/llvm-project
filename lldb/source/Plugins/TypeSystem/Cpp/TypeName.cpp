//===-- TypeName.cpp ------------------------------------------------------===//
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
#include "TypeCpp.h"
#include "TypeObjC.h"

#include "lldb/Symbol/Type.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/FormatVariadic.h"

#include <cstdio>

namespace lldb_private {
// NB: everything below sits inside lldb_private rather than pulling it in with
// a using-directive. lldb/Symbol/Type.h (included for GetTypeScopeAndBasename)
// declares an unrelated lldb_private::Type, and importing both namespaces at
// file scope would make every bare `Type` here ambiguous with the
// cpp_typesystem one this file is all about; inside lldb_private the inner
// cpp_typesystem::Type wins.
using namespace lldb_private::cpp_typesystem;


namespace {

// Append the namespace qualification for `ns` (outermost first), skipping
// inline namespaces so that e.g. `std::__1` prints as `std::`, and anonymous
// namespaces so that a type in one prints unqualified (e.g. `Bar`, matching
// clang, which elides its `(anonymous namespace)` scope).
static void AppendNamespacePrefix(const cpp_typesystem::Namespace *ns,
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
// the modeled template arguments (see NeedsTemplateNameReconstruction)
// instead of taking the DWARF spelling verbatim.
static void AppendQualifiedNamespacePrefix(const cpp_typesystem::Namespace *ns,
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
static void AppendUnnamedTagNamespacePrefix(const cpp_typesystem::Namespace *ns,
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

// Count the enclosing namespaces of `ns` (including inline ones), i.e. how many
// leading scope components of a qualified name are namespaces rather than
// enclosing classes.
static unsigned CountNamespaceDepth(const cpp_typesystem::Namespace *ns) {
  unsigned depth = 0;
  for (; ns; ns = ns->GetParent())
    ++depth;
  return depth;
}

// A type nested inside a class (e.g. `Foo<int>::Nested<char>`) is only modeled
// with its enclosing *namespaces*; the enclosing class scopes are not. Recover
// them from the type's fully-qualified DWARF spelling: parse it into scope
// components, drop the leading ones that correspond to the modeled namespaces,
// and append whatever class scopes remain (e.g. `Foo<int>::`).
static void AppendClassScopePrefix(llvm::StringRef qualified_name,
                                   const cpp_typesystem::Namespace *ns,
                                   std::string &out) {
  std::optional<lldb_private::Type::ParsedName> parsed =
      lldb_private::Type::GetTypeScopeAndBasename(qualified_name);
  if (!parsed)
    return;
  llvm::ArrayRef<llvm::StringRef> scope = parsed->scope;
  // Skip a leading "::" (global-scope marker) and the namespace components,
  // which the caller has already emitted (with inline namespaces hidden).
  if (!scope.empty() && scope.front() == "::")
    scope = scope.drop_front();
  unsigned ns_depth = CountNamespaceDepth(ns);
  if (scope.size() <= ns_depth)
    return;
  for (llvm::StringRef component : scope.drop_front(ns_depth)) {
    out += component.str();
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
static std::string BuildUnnamedTagName(cpp_typesystem::Type *t) {
  using namespace cpp_typesystem;
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

static std::string BuildDisplayNameImpl(cpp_typesystem::Type *t,
                                    bool hide_default_args = true,
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
static void AppendAnonymousParentPrefix(cpp_typesystem::Type *t,
                                        std::string &out) {
  auto *rec = llvm::dyn_cast<cpp_typesystem::RecordType>(t);
  if (!rec)
    return;
  const cpp_typesystem::RecordType *parent = rec->GetAnonymousParent();
  if (!parent)
    return;
  out += BuildDisplayNameImpl(const_cast<cpp_typesystem::RecordType *>(parent));
  out += "::";
}

// Render an ARMv8.3 pointer-auth qualifier as `__ptrauth(key,addr_disc,extra)`,
// matching clang's TypePrinter / TypeSystemClang spelling.
static std::string BuildPtrAuthQualifier(const cpp_typesystem::PtrAuthType *pa) {
  return llvm::formatv("__ptrauth({0},{1},{2})", pa->GetKey(),
                       pa->IsAddressDiscriminated() ? 1 : 0,
                       pa->GetExtraDiscriminator())
      .str();
}

// Render a function signature in C declarator form, placing `decl` (e.g. "" for
// a plain function, "(*)" for a function pointer, "(&)" for a reference) between
// the return type and the parameter list: `int (*)(const char *)`.
static std::string BuildFunctionNameImpl(cpp_typesystem::FunctionType *fn,
                                     llvm::StringRef decl,
                                     bool keep_inline_namespaces = false) {
  std::string ret = fn->GetReturnType()
                        ? BuildDisplayNameImpl(fn->GetReturnType(),
                                           /*hide_default_args=*/true,
                                           keep_inline_namespaces)
                        : std::string("void");
  std::string params;
  for (uint32_t i = 0, e = fn->GetNumParameters(); i != e; ++i) {
    if (!params.empty())
      params += ", ";
    params += BuildDisplayNameImpl(fn->GetParameterAtIndex(i),
                               /*hide_default_args=*/true,
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
static std::string
BuildFloatArgName(const cpp_typesystem::TemplateArgument &arg,
                  cpp_typesystem::Type *value_type) {
  std::optional<uint64_t> byte_size = value_type->GetByteSize();
  // Decode the raw bits into a double, then format with C's `%e`.
  double d = 0.0;
  if (byte_size == 8) {
    d = llvm::bit_cast<double>(arg.integral_value);
  } else if (byte_size == 4) {
    d = llvm::bit_cast<float>(static_cast<uint32_t>(arg.integral_value));
  } else if (byte_size == 2) {
    // Half (__fp16/_Float16) and bfloat (__bf16) share a 16-bit width but use
    // different semantics; distinguish by the type's spelling.
    const llvm::fltSemantics &sem =
        value_type->GetName().GetName().contains("bf16")
            ? llvm::APFloat::BFloat()
            : llvm::APFloat::IEEEhalf();
    llvm::APFloat f(sem, llvm::APInt(16, arg.integral_value));
    d = f.convertToDouble();
  } else {
    return std::to_string(arg.integral_value);
  }
  char buf[64];
  ::snprintf(buf, sizeof(buf), "%e", d);
  return buf;
}

// Render a single template argument for the display name. `hide_default_args`
// is threaded into any nested type arguments so a fully-qualified name keeps
// defaulted template arguments while a simplified display name drops them.
// `keep_inline_namespaces` likewise threads through to nested type arguments:
// set when reconstructing the fully-qualified (non-display) name, which must
// keep inline namespaces (e.g. `std::__1::`) since data formatters key on
// that raw spelling.
static std::string
BuildTemplateArgName(const cpp_typesystem::TemplateArgument &arg,
                     bool hide_default_args,
                     bool keep_inline_namespaces = false) {
  if (arg.kind == lldb::eTemplateArgumentKindType)
    return arg.type.Get() ? BuildDisplayNameImpl(arg.type.Get(), hide_default_args,
                                             keep_inline_namespaces)
                          : std::string("void");
  // A template-template argument (e.g. `T1` in `C<float, T1>`) is kept by name
  // only; print that name.
  if (arg.kind == lldb::eTemplateArgumentKindTemplate)
    return arg.name.GetName().str();
  // Integral (non-type) argument: render according to the argument's type,
  // mirroring clang's TemplateArgument::print.
  cpp_typesystem::Type *value_type = arg.type.Get();
  if (value_type) {
    // An enum-typed argument prints as `EnumName::Enumerator` when the value
    // matches one of the enumerators, mirroring clang's TemplateArgument::print
    // (which uses the matching enumerator's name). If no enumerator matches,
    // clang falls back to the cast form `(EnumName)value`.
    if (auto *enum_type = llvm::dyn_cast<cpp_typesystem::EnumType>(value_type)) {
      std::string enum_name =
          BuildDisplayNameImpl(enum_type, hide_default_args, keep_inline_namespaces);
      for (const cpp_typesystem::Enumerator &e : enum_type->GetEnumerators()) {
        if (e.value == arg.integral_value)
          return enum_name + "::" + e.name.GetName().str();
      }
      std::string value =
          enum_type->IsSigned()
              ? std::to_string(static_cast<int64_t>(arg.integral_value))
              : std::to_string(arg.integral_value);
      return "(" + enum_name + ")" + value;
    }
    // A `char`-family argument prints as a character literal ('v'), a `bool`
    // argument prints as true/false, and a floating-point argument prints in
    // scientific form.
    switch (value_type->GetFormat()) {
    case lldb::eFormatChar:
      return BuildCharLiteral(static_cast<unsigned char>(arg.integral_value));
    case lldb::eFormatBoolean:
      return arg.integral_value ? "true" : "false";
    default:
      break;
    }
    if (value_type->GetEncoding() == lldb::eEncodingIEEE754)
      return BuildFloatArgName(arg, value_type);
    // A pointer/reference-typed argument (e.g. `&temp1.member`) has no integral
    // value to print; clang falls back to printing the argument's type name.
    if (llvm::isa<cpp_typesystem::PointerType>(value_type) ||
        llvm::isa<cpp_typesystem::ReferenceType>(value_type))
      return BuildDisplayNameImpl(value_type, hide_default_args,
                              keep_inline_namespaces);
  }
  // Other integral arguments: print the numeric value, honoring signedness.
  bool is_signed =
      !value_type || value_type->GetEncoding() == lldb::eEncodingSint;
  if (is_signed)
    return std::to_string(static_cast<int64_t>(arg.integral_value));
  return std::to_string(arg.integral_value);
}

// Build the `<...>` template-argument list of a class-template instantiation
// (e.g. `<int, EnumType::Member>`), reconstructing each argument from the type
// model. `hide_default_args` drops defaulted arguments (for the simplified
// display name) while keeping them for the fully-qualified name.
static std::string BuildTemplateArgList(cpp_typesystem::RecordType *rec,
                                        bool hide_default_args,
                                        bool keep_inline_namespaces = false) {
  std::string args;
  for (uint32_t i = 0; i < rec->GetNumTemplateArguments(); ++i) {
    const cpp_typesystem::TemplateArgument *arg =
        rec->GetTemplateArgumentAtIndex(i);
    if (hide_default_args && arg->is_default)
      continue;
    if (!args.empty())
      args += ", ";
    args += BuildTemplateArgName(*arg, hide_default_args, keep_inline_namespaces);
  }
  // No (non-default) arguments, e.g. a specialization over an empty pack. Clang
  // still prints an empty argument list: `TypePack<>`.
  if (args.empty())
    return "<>";
  // Match clang's default printing policy (SplitTemplateClosers): put a space
  // between two consecutive closing angle brackets so a nested instantiation
  // prints as `Foo<Foo<int> >`, not `Foo<Foo<int>>`.
  std::string closer = args.back() == '>' ? " >" : ">";
  return "<" + args + closer;
}

// True if \p rec is a class-template instantiation whose name needs to be
// reconstructed from the modeled template arguments rather than taken
// verbatim from the DWARF producer's spelling of the instantiation name. Two
// cases diverge from clang's own printing (TemplateArgument::print, which
// BuildTemplateArgName mirrors):
//  - an enum-typed argument: the DWARF producer renders the cast form
//    `Foo<(EnumType)0>`, clang prints the enumerator spelling
//    `Foo<EnumType::Member>`.
//  - any other non-`int` integral argument (e.g. `long`, `unsigned`): the
//    DWARF producer's DW_AT_name suffixes the literal to disambiguate its
//    type (`Foo<1L>`, `Foo<1U>`), which clang's printer never does (`Foo<1>`)
//    because the type is already known from context.
// This must recurse into type-kind template arguments (`Foo<Bar<1L>>`):
// `Bar<1L>`'s own spelling embeds the divergent argument, so `Foo`'s DWARF
// name is wrong even though none of `Foo`'s own template arguments is
// integral.
static bool NeedsTemplateNameReconstruction(cpp_typesystem::Type *t) {
  auto *rec = llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(t);
  if (!rec || !rec->IsTemplateInstantiation())
    return false;
  for (uint32_t i = 0, e = rec->GetNumTemplateArguments(); i != e; ++i) {
    const cpp_typesystem::TemplateArgument *arg =
        rec->GetTemplateArgumentAtIndex(i);
    if (arg->kind == lldb::eTemplateArgumentKindType) {
      if (NeedsTemplateNameReconstruction(arg->type.Get()))
        return true;
      continue;
    }
    if (arg->kind != lldb::eTemplateArgumentKindIntegral)
      continue;
    cpp_typesystem::Type *value_type = arg->type.Get();
    if (llvm::isa_and_nonnull<cpp_typesystem::EnumType>(value_type))
      return true;
    if (auto *builtin =
            llvm::dyn_cast_or_null<cpp_typesystem::BuiltinType>(value_type))
      if (builtin->GetBuiltinKind() != cpp_typesystem::BuiltinKind::Int)
        return true;
  }
  return false;
}

// Build a type's (simplified) display name from the type model: qualified with
// its declaring namespaces (inline ones skipped) and, for a class-template
// instantiation, its template arguments with defaulted ones hidden.
static std::string BuildDisplayNameImpl(cpp_typesystem::Type *t,
                                    bool hide_default_args,
                                    bool keep_inline_namespaces) {
  using namespace cpp_typesystem;
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
                                            hide_default_args,
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
    cpp_typesystem::Type *cur = t;
    while (auto *array = llvm::dyn_cast<ArrayType>(cur)) {
      if (std::optional<uint64_t> n = array->GetNumElements())
        dims += llvm::formatv("[{0}]", *n).str();
      else
        dims += "[]";
      cur = array->GetElementType();
    }
    return BuildDisplayNameImpl(cur, hide_default_args, keep_inline_namespaces) +
           dims;
  }
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (auto *fn = llvm::dyn_cast_or_null<FunctionType>(pointee))
      return BuildFunctionNameImpl(
          fn, llvm::isa<BlockPointerType>(ptr) ? "(^)" : "(*)",
          keep_inline_namespaces);
    std::string pointee_name =
        pointee ? BuildDisplayNameImpl(pointee, hide_default_args,
                                   keep_inline_namespaces)
                : std::string("void");
    // Clang omits the space between a pointer/reference sigil and a following
    // '*' (e.g. "int **", "void **", "int &*"), but keeps it after a plain
    // type name ("int *").
    const bool tight = !pointee_name.empty() &&
                       (pointee_name.back() == '*' || pointee_name.back() == '&');
    return pointee_name + (tight ? "*" : " *");
  }
  if (auto *ref = llvm::dyn_cast<ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (auto *fn = llvm::dyn_cast_or_null<FunctionType>(pointee))
      return BuildFunctionNameImpl(fn, ref->IsRValue() ? "(&&)" : "(&)",
                               keep_inline_namespaces);
    std::string pointee_name =
        pointee ? BuildDisplayNameImpl(pointee, hide_default_args,
                                   keep_inline_namespaces)
                : std::string("void");
    const bool tight = !pointee_name.empty() &&
                       (pointee_name.back() == '*' || pointee_name.back() == '&');
    llvm::StringRef sigil = ref->IsRValue() ? "&&" : "&";
    return pointee_name + (tight ? sigil.str() : (" " + sigil.str()));
  }
  if (auto *fn = llvm::dyn_cast<FunctionType>(t))
    return BuildFunctionNameImpl(fn, "", keep_inline_namespaces);
  if (auto *cx = llvm::dyn_cast<ComplexType>(t)) {
    std::string element = cx->GetElementType()
                              ? BuildDisplayNameImpl(cx->GetElementType(),
                                                 hide_default_args,
                                                 keep_inline_namespaces)
                              : std::string("float");
    return "_Complex " + element;
  }
  if (auto *cv = llvm::dyn_cast<CVQualifiedType>(t)) {
    std::string qualifier;
    if (cv->IsConst())
      qualifier += "const";
    if (cv->IsVolatile())
      qualifier += qualifier.empty() ? "volatile" : " volatile";
    std::string underlying = cv->GetUnderlyingType()
                                 ? BuildDisplayNameImpl(cv->GetUnderlyingType(),
                                                     hide_default_args,
                                                     keep_inline_namespaces)
                                 : "";
    // A cv-qualifier on a pointer itself is a declarator suffix (`T *const`),
    // not a prefix (`const T *`, which would instead qualify the pointee) --
    // matching the PtrAuthType case below and clang's own type printer. This
    // only applies to the pointer directly under this qualifier: a qualified
    // pointee (`const T *const`) has already rendered its own leading `const`
    // as part of `underlying` (via the pointee's own CVQualifiedType), so no
    // double-prefixing happens here.
    if (llvm::isa_and_nonnull<PointerType>(cv->GetUnderlyingType())) {
      // No space between the `*` and the qualifier (`T *const`), matching
      // clang's declarator-suffix spelling.
      const bool tight =
          !underlying.empty() && underlying.back() == '*';
      return underlying + (tight ? "" : " ") + qualifier;
    }
    return qualifier + (qualifier.empty() ? "" : " ") + underlying;
  }
  if (auto *pa = llvm::dyn_cast<PtrAuthType>(t)) {
    std::string underlying = BuildDisplayNameImpl(
        pa->GetUnderlyingType(), hide_default_args, keep_inline_namespaces);
    std::string qualifier = BuildPtrAuthQualifier(pa);
    // Clang spells the qualifier as a declarator suffix on a pointer
    // (`int *__ptrauth(...)`) but as a prefix otherwise (`__ptrauth(...) intp`).
    if (llvm::isa_and_nonnull<PointerType>(pa->GetUnderlyingType()))
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
    // An unnamed record/enum prints as "(unnamed struct)" etc. (matching clang);
    // builtins have no unqualified name but do carry a stored name. An
    // anonymous struct/union additionally gets its enclosing record's name
    // prefixed (e.g. "MySock::(anonymous union)"), since clang's own printer
    // qualifies it that way (see AppendAnonymousParentPrefix). Otherwise (e.g.
    // a lambda closure type, which has no DW_AT_name but does have a DeclContext)
    // qualify by the enclosing namespace chain instead, same as a named type --
    // this is what makes a lambda defined in an anonymous namespace print as
    // "(anonymous namespace)::(unnamed class)".
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
  AppendClassScopePrefix(t->GetName().GetName(), t->GetDeclContext(), result);

  // For a class-template instantiation we have modeled args, so reconstruct
  // "base<non-default args>". This branch is also taken for a specialization
  // over an empty parameter pack (`TypePack<>`), which has zero arguments but
  // still prints an (empty) `<>`. Otherwise use the unqualified spelling
  // verbatim (this also covers not-yet-completed templates, whose args aren't
  // parsed).
  auto *rec = llvm::dyn_cast<RecordType>(t);
  if (rec && rec->IsTemplateInstantiation()) {
    result += unqualified.substr(0, unqualified.find('<')).str();
    result += BuildTemplateArgList(rec, hide_default_args, keep_inline_namespaces);
  } else {
    result += unqualified.str();
  }
  return result;
}

} // namespace

// The exported entry points. BuildDisplayName/BuildFunctionName just name the
// file-local implementations above; the recursion between them all stays
// internal.

std::string cpp_typesystem::BuildDisplayName(Type *t, bool hide_default_args,
                                             bool keep_inline_namespaces) {
  return BuildDisplayNameImpl(t, hide_default_args, keep_inline_namespaces);
}

std::string cpp_typesystem::BuildFunctionName(FunctionType *fn,
                                              llvm::StringRef decl,
                                              bool keep_inline_namespaces) {
  return BuildFunctionNameImpl(fn, decl, keep_inline_namespaces);
}

std::string cpp_typesystem::BuildCanonicalName(Type *t, bool base_only) {
  if (!t)
    return std::string();
  // Strip elaborated display sugar (e.g. the `::` of `::Struct`, or template
  // spelling sugar) from the canonical name: the source spelling only affects
  // the *display* name, so a formatter keyed on `Struct` still matches a
  // `::Struct`-spelled value. Typedefs are kept (they are meaningful, distinct
  // names).
  t = ElaboratedType::Strip(t);
  // Arrays have no name of their own; build "<element>[<count>]". For a
  // multidimensional array the nesting is outermost-dimension first, so peel
  // the whole chain and print the dimensions in source order after the
  // innermost element's name.
  if (llvm::isa<ArrayType>(t)) {
    // A vector type (DW_AT_GNU_vector) is spelled like clang's
    // `<element> __attribute__((ext_vector_type(N)))` rather than as an array
    // (`<element>[N]`), so it doesn't get matched by array/char-array
    // formatters (matching TypeSystemClang's type name for vectors).
    if (auto *vec = llvm::dyn_cast<ArrayType>(t);
        vec && vec->IsVector()) {
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
  // Function types and pointers/references to them need C declarator syntax
  // (`int (*)(const char *)`); BuildDisplayNameImpl already produces it.
  if (llvm::isa<FunctionType>(t))
    return BuildDisplayNameImpl(t);
  if (llvm::isa<ComplexType>(t))
    return BuildDisplayNameImpl(t);
  // Pointers have no name of their own either; build "<pointee> *".
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    Type *pointee = ptr->GetPointeeType();
    if (llvm::isa_and_nonnull<FunctionType>(pointee))
      return BuildDisplayNameImpl(t);
    std::string pointee_name =
        pointee ? BuildCanonicalName(pointee, base_only) : "void";
    // Clang omits the space before '*' when the pointee already ends in a
    // pointer/reference sigil ("void **", "int *&"), so keep the two tight.
    const bool tight =
        !pointee_name.empty() &&
        (pointee_name.back() == '*' || pointee_name.back() == '&');
    return pointee_name + (tight ? "*" : " *");
  }
  // References likewise: "<pointee> &" or "<pointee> &&".
  if (auto *ref = llvm::dyn_cast<ReferenceType>(t)) {
    Type *pointee = ref->GetPointeeType();
    if (llvm::isa_and_nonnull<FunctionType>(pointee))
      return BuildDisplayNameImpl(t);
    std::string pointee_name =
        pointee ? BuildCanonicalName(pointee, base_only) : "void";
    const bool tight =
        !pointee_name.empty() &&
        (pointee_name.back() == '*' || pointee_name.back() == '&');
    llvm::StringRef sigil = ref->IsRValue() ? "&&" : "&";
    return pointee_name + (tight ? sigil.str() : (" " + sigil.str()));
  }
  // cv-qualified types render as "const"/"volatile" prefixing the unqualified
  // name. Matching TypeSystemClang's GetTypeName: the cv-qualifiers are only
  // spelled for types whose name is printed from the QualType (builtins,
  // pointers, references, ...). For a tag type (record/enum) or a typedef the
  // name is taken from the underlying decl, which carries no qualifiers, so
  // `const Enum` prints as `Enum` (while `const int` stays `const int`).
  if (auto *cv = llvm::dyn_cast<CVQualifiedType>(t)) {
    std::string underlying_name =
        cv->GetUnderlyingType() ? BuildCanonicalName(cv->GetUnderlyingType(), base_only)
                                : "";
    // Look through display/cv sugar to the leaf type to decide whether the
    // qualifiers are spelled.
    Type *leaf = cv->GetUnderlyingType();
    while (leaf) {
      if (auto *el = llvm::dyn_cast<ElaboratedType>(leaf))
        leaf = el->GetUnderlyingType();
      else if (auto *inner =
                   llvm::dyn_cast<CVQualifiedType>(leaf))
        leaf = inner->GetUnderlyingType();
      else
        break;
    }
    const bool drop_qualifiers =
        leaf && (llvm::isa<RecordType>(leaf) ||
                 llvm::isa<EnumType>(leaf) ||
                 llvm::isa<TypedefType>(leaf));
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
    if (llvm::isa_and_nonnull<PointerType>(
            pa->GetUnderlyingType()))
      return underlying + qualifier;
    return qualifier + " " + underlying;
  }
  // Named leaf type (record/typedef/enum/builtin). `base_only` asks for the
  // unqualified spelling (no enclosing scopes), matching clang's
  // GetTypeNameForDecl(qualified=false); this is what SymbolFileDWARF::FindTypes
  // compares against a template query's basename (e.g. "Nested<char>", not the
  // scoped "Foo<int>::Nested<char>"). Builtins carry no unqualified name, so
  // fall back to the full name for them.
  //
  // The name normally comes verbatim from the debug info -- data formatters
  // (e.g. libc++'s) match against exactly that spelling, so it must be
  // preserved. The one exception is a class-template instantiation with an
  // enum-typed non-type argument: the DWARF producer renders it as the cast
  // form `(EnumType)0`, whereas clang (and thus every name-based consumer)
  // expects the enumerator spelling `EnumType::Member`. Only in that case do we
  // reconstruct the name from the modeled template arguments. The
  // fully-qualified name keeps defaulted arguments; only GetDisplayTypeName
  // drops them.
  if (auto *rec = llvm::dyn_cast<RecordType>(t)) {
    if (NeedsTemplateNameReconstruction(rec)) {
      llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
      std::string base = unqualified.substr(0, unqualified.find('<')).str();
      std::string args = BuildTemplateArgList(rec, /*hide_default_args=*/false,
                                              /*keep_inline_namespaces=*/true);
      if (base_only)
        return base + args;
      // Fully-qualified: prefix the enclosing namespace/class scopes, keeping
      // inline namespaces (unlike the display name) since this is the raw
      // spelling data formatters key on.
      std::string result;
      AppendQualifiedNamespacePrefix(t->GetDeclContext(), result);
      AppendClassScopePrefix(t->GetName().GetName(), t->GetDeclContext(),
                             result);
      return result + base + args;
    }
  }
  if (base_only) {
    llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
    if (!unqualified.empty())
      return unqualified.str();
  }
  // An unnamed record/enum has no spelling in the debug info; render it as
  // "(unnamed struct)" etc. to match clang / TypeSystemClang. An anonymous
  // struct/union additionally gets its enclosing record's name prefixed (e.g.
  // "MySock::(anonymous union)"); see AppendAnonymousParentPrefix. Otherwise
  // (e.g. a lambda closure type) qualify by the enclosing namespace chain
  // instead, same as the named-type case above (keeping inline namespaces,
  // matching the raw-spelling convention this function otherwise uses).
  // `base_only` asks for the unqualified spelling, so skip both prefixes in
  // that case.
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
