//===-- TypeSystemCpp.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemCpp.h"

#include "Plugins/SymbolFile/DWARF/DWARFASTParserCpp.h"
#include "Plugins/SymbolFile/DWARF/DWARFDIE.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"

#include "Plugins/ExpressionParser/Clang/ClangASTGenerator.h"
#include "Plugins/ExpressionParser/Clang/ClangFunctionCaller.h"
#include "Plugins/ExpressionParser/Clang/ClangPersistentVariables.h"
#include "Plugins/ExpressionParser/Clang/ClangUserExpression.h"
#include "Plugins/ExpressionParser/Clang/ClangUtilityFunction.h"

#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Stream.h"
#include "lldb/ValueObject/ValueObject.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cstdio>

using namespace lldb_private;
using namespace lldb;

using cpp_typesystem::Field;

/// Peel typedef/cv-qualifier "sugar" off a type to reach its canonical type.
/// Mirrors clang's RemoveWrappingTypes: queries about layout and children
/// should see through aliases and qualifiers.
static cpp_typesystem::Type *Desugar(cpp_typesystem::Type *t) {
  while (auto *sugar = llvm::dyn_cast_or_null<cpp_typesystem::SugarType>(t))
    t = sugar->GetUnderlyingType();
  return t;
}

// Whether a record type has any data members, considering base classes
// recursively. A vtable pointer is not a field, so a polymorphic-but-otherwise-
// empty class returns false here. This mirrors TypeSystemClang::RecordHasFields
// and drives whether an empty base class is omitted from a value's children
// (see the omit_empty_base_classes handling below). `complete` completes each
// (lazily-parsed) base before inspecting it so its field count is known.
static bool RecordHasFields(cpp_typesystem::Type *t,
                            llvm::function_ref<void(cpp_typesystem::Type *)>
                                complete) {
  t = Desugar(t);
  if (!t)
    return false;
  complete(t);
  if (t->GetNumFields() != 0)
    return true;
  // We always want a record with no definition anywhere in the debug info
  // (e.g. -flimit-debug-info) to show up, so we can print a message in the
  // summary indicating that the type is incomplete: otherwise a base class
  // in this state would be silently hidden by the omit-empty-base-classes
  // logic below (since it looks exactly like an empty-but-complete base),
  // and a top-level variable of such a type would show nothing at all.
  // Mirrors TypeSystemClang::RecordHasFields's IsForcefullyCompleted check.
  if (!t->IsComplete())
    return true;
  for (uint32_t i = 0, n = t->GetNumBaseClasses(); i < n; ++i) {
    const cpp_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    if (base && RecordHasFields(base->type.Get(), complete))
      return true;
  }
  return false;
}

// Compute the byte offset of a virtual base subobject within a live derived
// object, using the Itanium-ABI vtable slot recorded on the base. `base` must
// be a virtual base with a vbase_offset_offset. `valobj` is the derived object
// being inspected. Reads the object's vtable pointer, steps back
// vbase_offset_offset bytes to the slot holding this base's offset, and returns
// that offset (relative to the derived object's start). Returns nullopt if the
// process/object isn't available or the reads fail, so the caller falls back to
// the (static, possibly wrong) byte_offset. Mirrors
// TypeSystemClang::GetVBaseBitOffset.
static std::optional<int64_t>
ReadVirtualBaseOffset(const cpp_typesystem::BaseClass &base,
                      ValueObject *valobj) {
  if (!valobj || !base.vbase_offset_offset)
    return std::nullopt;
  ExecutionContext exe_ctx(valobj->GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();
  if (!process)
    return std::nullopt;

  // The vtable pointer sits at the start of the (derived) object. When the
  // vbase is reached transparently through a pointer (see the transparent
  // pointer forwarding in GetChildCompilerTypeAtIndex), `valobj` is the pointer
  // itself: the derived object is what it points to, so use the pointee (load)
  // address rather than where the pointer is stored.
  lldb::addr_t obj_addr = LLDB_INVALID_ADDRESS;
  if (valobj->IsPointerType()) {
    ValueObject::AddrAndType ptr = valobj->GetPointerValue();
    if (ptr.type != eAddressTypeLoad)
      return std::nullopt;
    obj_addr = ptr.address;
  } else {
    ValueObject::AddrAndType addr = valobj->GetAddressOf();
    if (addr.type != eAddressTypeLoad)
      return std::nullopt;
    obj_addr = addr.address;
  }
  if (obj_addr == LLDB_INVALID_ADDRESS || obj_addr == 0)
    return std::nullopt;

  Status err;
  lldb::addr_t vtable_ptr = process->ReadPointerFromMemory(obj_addr, err);
  if (err.Fail() || vtable_ptr == LLDB_INVALID_ADDRESS)
    return std::nullopt;

  const uint32_t addr_size = process->GetAddressByteSize();
  int64_t offset = process->ReadSignedIntegerFromMemory(
      vtable_ptr - *base.vbase_offset_offset, addr_size, INT64_MAX, err);
  if (err.Fail() || offset == INT64_MAX)
    return std::nullopt;
  return offset;
}

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

static std::string BuildDisplayName(cpp_typesystem::Type *t,
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
  out += BuildDisplayName(const_cast<cpp_typesystem::RecordType *>(parent));
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

// Look through transparent sugar (typedef/cv/elaborated) for the pointer-auth
// qualifier that applies to \p type, or null if none. The `__ptrauth` qualifier
// sits on the outermost declarator, so only see-through sugar is peeled.
static const cpp_typesystem::PtrAuthType *
FindPtrAuthType(lldb::opaque_compiler_type_t type) {
  cpp_typesystem::Type *t = TypeSystemCpp::GetCppType(type);
  while (t) {
    if (auto *pa = llvm::dyn_cast<cpp_typesystem::PtrAuthType>(t))
      return pa;
    if (auto *sugar = llvm::dyn_cast<cpp_typesystem::SugarType>(t)) {
      t = sugar->GetUnderlyingType();
      continue;
    }
    break;
  }
  return nullptr;
}

// Render a function signature in C declarator form, placing `decl` (e.g. "" for
// a plain function, "(*)" for a function pointer, "(&)" for a reference) between
// the return type and the parameter list: `int (*)(const char *)`.
static std::string BuildFunctionName(cpp_typesystem::FunctionType *fn,
                                     llvm::StringRef decl,
                                     bool keep_inline_namespaces = false) {
  std::string ret = fn->GetReturnType()
                        ? BuildDisplayName(fn->GetReturnType(),
                                           /*hide_default_args=*/true,
                                           keep_inline_namespaces)
                        : std::string("void");
  std::string params;
  for (uint32_t i = 0, e = fn->GetNumParameters(); i != e; ++i) {
    if (!params.empty())
      params += ", ";
    params += BuildDisplayName(fn->GetParameterAtIndex(i),
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
    return arg.type.Get() ? BuildDisplayName(arg.type.Get(), hide_default_args,
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
          BuildDisplayName(enum_type, hide_default_args, keep_inline_namespaces);
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
      return BuildDisplayName(value_type, hide_default_args,
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
static std::string BuildDisplayName(cpp_typesystem::Type *t,
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
                           BuildDisplayName(vec->GetElementType(),
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
    return BuildDisplayName(cur, hide_default_args, keep_inline_namespaces) +
           dims;
  }
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (auto *fn = llvm::dyn_cast_or_null<FunctionType>(pointee))
      return BuildFunctionName(fn, ptr->IsBlockPointer() ? "(^)" : "(*)",
                               keep_inline_namespaces);
    std::string pointee_name =
        pointee ? BuildDisplayName(pointee, hide_default_args,
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
      return BuildFunctionName(fn, ref->IsRValue() ? "(&&)" : "(&)",
                               keep_inline_namespaces);
    std::string pointee_name =
        pointee ? BuildDisplayName(pointee, hide_default_args,
                                   keep_inline_namespaces)
                : std::string("void");
    const bool tight = !pointee_name.empty() &&
                       (pointee_name.back() == '*' || pointee_name.back() == '&');
    llvm::StringRef sigil = ref->IsRValue() ? "&&" : "&";
    return pointee_name + (tight ? sigil.str() : (" " + sigil.str()));
  }
  if (auto *fn = llvm::dyn_cast<FunctionType>(t))
    return BuildFunctionName(fn, "", keep_inline_namespaces);
  if (auto *cx = llvm::dyn_cast<ComplexType>(t)) {
    std::string element = cx->GetElementType()
                              ? BuildDisplayName(cx->GetElementType(),
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
                                 ? BuildDisplayName(cv->GetUnderlyingType(),
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
    std::string underlying = BuildDisplayName(
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


LLDB_PLUGIN_DEFINE(TypeSystemCpp)

char TypeSystemCpp::ID;
char ScratchTypeSystemCpp::ID;

TypeSystemCpp::TypeSystemCpp(llvm::StringRef name, llvm::Triple triple)
    : m_display_name(name.str()), m_triple(std::move(triple)),
      m_context(cpp_typesystem::LanguageOpts(m_triple)) {}

TypeSystemCpp::~TypeSystemCpp() = default;

plugin::dwarf::DWARFASTParser *TypeSystemCpp::GetDWARFParser() {
  if (!m_dwarf_ast_parser_up)
    m_dwarf_ast_parser_up = std::make_unique<DWARFASTParserCpp>(*this);
  return m_dwarf_ast_parser_up.get();
}

CompilerType TypeSystemCpp::GetCompilerType(cpp_typesystem::Type *type) {
  return CompilerType(weak_from_this(), type);
}

lldb::TypeSystemSP TypeSystemCpp::Create(llvm::StringRef name,
                                         llvm::Triple triple) {
  return std::make_shared<TypeSystemCpp>(name, std::move(triple));
}

TypeSystemSP TypeSystemCpp::CreateInstance(LanguageType language,
                                           Module *module, Target *target) {
  return TypeSystemSP();
}

LanguageSet TypeSystemCpp::GetSupportedLanguagesForTypes() {
  return LanguageSet();
}

LanguageSet TypeSystemCpp::GetSupportedLanguagesForExpressions() {
  return LanguageSet();
}

void TypeSystemCpp::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "C/C++/Objective-C++ TypeSystem plug-in",
                                CreateInstance, GetSupportedLanguagesForTypes(),
                                GetSupportedLanguagesForExpressions());
}

void TypeSystemCpp::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ScratchTypeSystemCpp::ScratchTypeSystemCpp(Target &target, llvm::Triple triple)
    : TypeSystemCpp(std::string("scratch TypeSystemCpp for ") +
                        target.GetArchitecture().GetArchitectureName(),
                    std::move(triple)),
      m_target_wp(target.shared_from_this()) {}

UserExpression *ScratchTypeSystemCpp::GetUserExpression(
    llvm::StringRef expr, llvm::StringRef prefix, SourceLanguage language,
    Expression::ResultType desired_type,
    const EvaluateExpressionOptions &options, ValueObject *ctx_obj) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return nullptr;
  return new ClangUserExpression(*target, expr, prefix, language, desired_type,
                                 options, ctx_obj);
}

FunctionCaller *ScratchTypeSystemCpp::GetFunctionCaller(
    const CompilerType &return_type, const Address &function_address,
    const ValueList &arg_value_list, const char *name) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return nullptr;
  Process *process = target->GetProcessSP().get();
  if (!process)
    return nullptr;
  return new ClangFunctionCaller(*process, return_type, function_address,
                                 arg_value_list, name);
}

std::unique_ptr<UtilityFunction>
ScratchTypeSystemCpp::CreateUtilityFunction(std::string text,
                                            std::string name) {
  TargetSP target = m_target_wp.lock();
  if (!target)
    return {};
  return std::make_unique<ClangUtilityFunction>(
      *target, std::move(text), std::move(name),
      target->GetDebugUtilityExpression());
}

PersistentExpressionState *
ScratchTypeSystemCpp::GetPersistentExpressionState() {
  if (!m_persistent_variables) {
    TargetSP target = m_target_wp.lock();
    if (!target)
      return nullptr;
    m_persistent_variables =
        std::make_unique<ClangPersistentVariables>(target->shared_from_this());
  }
  return m_persistent_variables.get();
}

ConstString TypeSystemCpp::DeclGetName(void *opaque_decl) {
  // TypeSystemCpp's CompilerDecls are tagged cpp_typesystem::Decl references.
  auto *decl = static_cast<const cpp_typesystem::Decl *>(opaque_decl);
  if (!decl)
    return ConstString();
  switch (decl->kind) {
  case cpp_typesystem::Decl::Kind::StaticDataMember:
    return ConstString(
        static_cast<const cpp_typesystem::StaticDataMember *>(decl->payload)
            ->name.GetName());
  case cpp_typesystem::Decl::Kind::MemberFunction:
    return ConstString(
        static_cast<const cpp_typesystem::MemberFunction *>(decl->payload)
            ->name.GetName());
  }
  return ConstString();
}

ConstString TypeSystemCpp::DeclGetMangledName(void *opaque_decl) {
  auto *decl = static_cast<const cpp_typesystem::Decl *>(opaque_decl);
  if (!decl)
    return ConstString();
  switch (decl->kind) {
  case cpp_typesystem::Decl::Kind::StaticDataMember:
    return ConstString(
        static_cast<const cpp_typesystem::StaticDataMember *>(decl->payload)
            ->mangled_name.GetName());
  case cpp_typesystem::Decl::Kind::MemberFunction:
    return ConstString(
        static_cast<const cpp_typesystem::MemberFunction *>(decl->payload)
            ->mangled_name.GetName());
  }
  return ConstString();
}

CompilerType TypeSystemCpp::GetTypeForDecl(void *opaque_decl) {
  auto *decl = static_cast<const cpp_typesystem::Decl *>(opaque_decl);
  if (!decl)
    return CompilerType();
  switch (decl->kind) {
  case cpp_typesystem::Decl::Kind::StaticDataMember:
    return GetCompilerType(
        static_cast<const cpp_typesystem::StaticDataMember *>(decl->payload)
            ->type.Get());
  case cpp_typesystem::Decl::Kind::MemberFunction:
    return GetCompilerType(
        static_cast<const cpp_typesystem::MemberFunction *>(decl->payload)
            ->type.Get());
  }
  return CompilerType();
}

Scalar TypeSystemCpp::DeclGetConstantValue(void *opaque_decl) {
  auto *decl = static_cast<const cpp_typesystem::Decl *>(opaque_decl);
  if (!decl || decl->kind != cpp_typesystem::Decl::Kind::StaticDataMember)
    return Scalar();
  auto *member =
      static_cast<const cpp_typesystem::StaticDataMember *>(decl->payload);
  if (!member->HasConstValue())
    return Scalar();
  cpp_typesystem::Type *type = member->type.Get();
  if (!type)
    return Scalar();
  cpp_typesystem::Type *desugared = Desugar(type);
  std::optional<uint64_t> byte_size = type->GetByteSize();
  if (!byte_size)
    return Scalar();
  // Interpret the raw constant bits using the member type's signedness so a
  // signed integral member (e.g. `static constexpr long = 47`) reads back
  // correctly.
  bool is_signed = desugared->GetEncoding() == lldb::eEncodingSint;
  llvm::APInt value(*byte_size * 8, *member->const_value, is_signed);
  return Scalar(llvm::APSInt(value, !is_signed));
}

ConstString TypeSystemCpp::DeclContextGetName(void *opaque_decl_ctx) {
  // A TypeSystemCpp CompilerDeclContext wraps a cpp_typesystem::Namespace (the
  // global namespace is a null opaque pointer, which is never a valid
  // CompilerDeclContext).
  if (!opaque_decl_ctx)
    return ConstString();
  auto *ns = static_cast<const cpp_typesystem::Namespace *>(opaque_decl_ctx);
  return ConstString(ns->GetName().GetName());
}

ConstString
TypeSystemCpp::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  if (!opaque_decl_ctx)
    return ConstString();
  auto *ns = static_cast<const cpp_typesystem::Namespace *>(opaque_decl_ctx);
  // Build "A::B::C" from the namespace chain, skipping the (transparent)
  // inline namespaces so the spelling matches the source.
  llvm::SmallVector<llvm::StringRef, 4> parts;
  for (const cpp_typesystem::Namespace *cur = ns; cur; cur = cur->GetParent()) {
    if (cur->IsInline())
      continue;
    parts.push_back(cur->GetName().GetName());
  }
  std::string qualified;
  for (llvm::StringRef part : llvm::reverse(parts)) {
    if (!qualified.empty())
      qualified += "::";
    qualified += part.str();
  }
  return ConstString(qualified);
}

bool TypeSystemCpp::DeclContextIsClassMethod(void *opaque_decl_ctx) {
  return false;
}

std::vector<lldb_private::CompilerContext>
TypeSystemCpp::DeclContextGetCompilerContext(void *opaque_decl_ctx) {
  // Build the CompilerContext chain (topmost namespace first) so a
  // namespace-scoped type query (TypeQuery(decl_ctx, name)) can match a type's
  // DWARF lookup context. Inline namespaces stay in the chain (they are
  // transparent for name printing but the DWARF context lists them too);
  // ContextMatches handles anonymous namespaces optionally.
  std::vector<lldb_private::CompilerContext> context;
  for (auto *ns = static_cast<const cpp_typesystem::Namespace *>(opaque_decl_ctx);
       ns; ns = ns->GetParent())
    context.push_back({CompilerContextKind::Namespace,
                       ConstString(ns->GetName().GetName())});
  std::reverse(context.begin(), context.end());
  return context;
}

bool TypeSystemCpp::DeclContextIsContainedInLookup(
    void *opaque_decl_ctx, void *other_opaque_decl_ctx) {
  // Namespaces are interned uniquely per Context, so identity is pointer
  // equality. The lookup of a namespace also transparently contains any inline
  // namespace nested (transitively through inline namespaces) inside it, so
  // walk `other` up through its inline-namespace parents looking for a match.
  auto *self = static_cast<const cpp_typesystem::Namespace *>(opaque_decl_ctx);
  auto *other =
      static_cast<const cpp_typesystem::Namespace *>(other_opaque_decl_ctx);
  for (const cpp_typesystem::Namespace *cur = other; cur;
       cur = cur->GetParent()) {
    if (cur == self)
      return true;
    // Keep ascending while we are inside a transparent namespace: members of an
    // inline namespace (and of an unnamed namespace, which is implicitly a
    // using-directive into its parent) are visible in the enclosing scope. A
    // named, non-inline namespace is a distinct lookup scope, so stop there.
    if (!cur->IsInline() && !cur->GetName().GetName().empty())
      break;
  }
  return false;
}

LanguageType TypeSystemCpp::DeclContextGetLanguage(void *opaque_decl_ctx) {
  return eLanguageTypeC_plus_plus;
}

#ifndef NDEBUG
bool TypeSystemCpp::Verify(opaque_compiler_type_t type) { return true; }
#endif

bool TypeSystemCpp::IsArrayType(opaque_compiler_type_t type,
                                CompilerType *element_type, uint64_t *size,
                                bool *is_incomplete) {
  if (element_type)
    element_type->Clear();
  if (size)
    *size = 0;
  if (is_incomplete)
    *is_incomplete = false;

  auto *array = llvm::dyn_cast_or_null<cpp_typesystem::ArrayType>(
      type ? Desugar(GetCppType(type)) : nullptr);
  if (!array)
    return false;
  // A vector type (DW_AT_GNU_vector) is laid out like an array but is not an
  // array type (matching TypeSystemClang, where a Vector/ExtVector is distinct
  // from ConstantArray). Reporting it as an array makes char-element vectors
  // match the char-array-to-string formatter and mis-render.
  if (array->IsVector())
    return false;

  if (element_type)
    *element_type = GetCompilerType(array->GetElementType());
  if (std::optional<uint64_t> num_elements = array->GetNumElements()) {
    if (size)
      *size = *num_elements;
  } else if (is_incomplete) {
    *is_incomplete = true;
  }
  return true;
}

bool TypeSystemCpp::IsAggregateType(opaque_compiler_type_t type) {
  return type && GetCppType(type)->IsAggregate();
}

bool TypeSystemCpp::IsCharType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // Strip typedef/cv sugar so `const char` (e.g. the pointee of `const char *`)
  // is recognized. Match clang's narrow-char set: char/signed char/unsigned
  // char (not wchar_t). Identify them by their canonical display format and
  // size rather than pointer identity, so this also works for a type reached
  // through another Context (e.g. an expression result mapped into the scratch
  // TypeSystemCpp).
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  auto *builtin = llvm::dyn_cast<cpp_typesystem::BuiltinType>(t);
  return builtin && builtin->GetFormat() == lldb::eFormatChar &&
         builtin->GetByteSize().value_or(0) == 1;
}

bool TypeSystemCpp::IsCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // Mirror TypeSystemClang: complete the type now (if it has a definition in
  // the debug info) so we can give the caller an accurate answer about whether
  // the type actually has a definition, rather than just its current internal
  // completeness state. Builtins, pointers, references, etc. report complete
  // via cpp_typesystem::Type::IsComplete(); only records/enums that were parsed
  // as forward declarations without a definition stay incomplete.
  return GetCompleteType(type);
}

bool TypeSystemCpp::IsDefined(opaque_compiler_type_t type) { return false; }

bool TypeSystemCpp::IsFloatingPointType(opaque_compiler_type_t type) {
  return type && GetCppType(type)->GetEncoding() == eEncodingIEEE754;
}

bool TypeSystemCpp::IsFunctionType(opaque_compiler_type_t type) {
  return type &&
         llvm::isa<cpp_typesystem::FunctionType>(Desugar(GetCppType(type)));
}

size_t
TypeSystemCpp::GetNumberOfFunctionArguments(opaque_compiler_type_t type) {
  if (!type)
    return 0;
  if (auto *fn = llvm::dyn_cast<cpp_typesystem::FunctionType>(
          Desugar(GetCppType(type))))
    return fn->GetNumParameters();
  return 0;
}

CompilerType
TypeSystemCpp::GetFunctionArgumentAtIndex(opaque_compiler_type_t type,
                                          const size_t index) {
  if (!type)
    return CompilerType();
  if (auto *fn = llvm::dyn_cast<cpp_typesystem::FunctionType>(
          Desugar(GetCppType(type))))
    return GetCompilerType(fn->GetParameterAtIndex(index));
  return CompilerType();
}

bool TypeSystemCpp::IsFunctionPointerType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // Only a *pointer* to a function is a function-pointer type. A reference to a
  // function (`void (&)(int)`) is not, matching clang's isFunctionPointerType()
  // (and TypeSystemClang). This distinction matters for formatting: the C++
  // "function pointer summary" only applies to pointers, so a function
  // reference must not pick it up.
  auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t);
  if (!ptr)
    return false;
  // A block pointer (`int (^)(int)`) is not a function-pointer type, matching
  // clang's isFunctionPointerType().
  if (ptr->IsBlockPointer())
    return false;
  cpp_typesystem::Type *pointee = ptr->GetPointeeType();
  return pointee && llvm::isa<cpp_typesystem::FunctionType>(Desugar(pointee));
}

bool TypeSystemCpp::IsMemberFunctionPointerType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  auto *mp = llvm::dyn_cast<cpp_typesystem::MemberPointerType>(
      Desugar(GetCppType(type)));
  if (!mp)
    return false;
  cpp_typesystem::Type *pointee = mp->GetPointeeType();
  return pointee && llvm::isa<cpp_typesystem::FunctionType>(Desugar(pointee));
}

bool TypeSystemCpp::IsMemberDataPointerType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  auto *mp = llvm::dyn_cast<cpp_typesystem::MemberPointerType>(
      Desugar(GetCppType(type)));
  if (!mp)
    return false;
  cpp_typesystem::Type *pointee = mp->GetPointeeType();
  return pointee && !llvm::isa<cpp_typesystem::FunctionType>(Desugar(pointee));
}

bool TypeSystemCpp::IsBlockPointerType(
    opaque_compiler_type_t type, CompilerType *function_pointer_type_ptr) {
  if (!type)
    return false;
  auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(
      Desugar(GetCppType(type)));
  if (!ptr || !ptr->IsBlockPointer())
    return false;
  // Report the corresponding function-pointer type (a plain pointer to the
  // block's function type), mirroring TypeSystemClang.
  if (function_pointer_type_ptr) {
    if (cpp_typesystem::Type *fn = ptr->GetPointeeType())
      *function_pointer_type_ptr =
          cpp_typesystem::Builder(*this).CreatePointerType(
              GetCompilerType(fn));
  }
  return true;
}

bool TypeSystemCpp::IsIntegerType(opaque_compiler_type_t type,
                                  bool &is_signed) {
  is_signed = false;
  if (!type)
    return false;
  // Only builtin integer types (and enumerations, matching TypeSystemCpp's
  // existing treatment) are integers. Pointers report an unsigned encoding for
  // value extraction but must NOT be classified as integers, or e.g. DIL
  // array-subscript index checking would accept a pointer index.
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  if (!llvm::isa<cpp_typesystem::BuiltinType, cpp_typesystem::EnumType>(t))
    return false;
  // std::nullptr_t is a builtin with an unsigned (pointer-width) encoding but
  // is not an integer type.
  if (auto *bt = llvm::dyn_cast<cpp_typesystem::BuiltinType>(t))
    if (bt->GetBuiltinKind() == cpp_typesystem::BuiltinKind::NullPtr)
      return false;
  switch (t->GetEncoding()) {
  case eEncodingSint:
    is_signed = true;
    return true;
  case eEncodingUint:
    return true;
  default:
    return false;
  }
}

bool TypeSystemCpp::IsScopedEnumerationType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  if (auto *enum_type =
          llvm::dyn_cast<cpp_typesystem::EnumType>(Desugar(GetCppType(type))))
    return enum_type->IsScoped();
  return false;
}

bool TypeSystemCpp::IsEnumerationType(opaque_compiler_type_t type,
                                      bool &is_signed) {
  is_signed = false;
  if (!type)
    return false;
  if (auto *enum_type =
          llvm::dyn_cast<cpp_typesystem::EnumType>(Desugar(GetCppType(type)))) {
    is_signed = enum_type->IsSigned();
    return true;
  }
  return false;
}

bool TypeSystemCpp::IsPossibleDynamicType(opaque_compiler_type_t type,
                                          CompilerType *target_type,
                                          bool check_cplusplus,
                                          bool check_objc) {
  if (target_type)
    target_type->Clear();
  if (!type)
    return false;

  cpp_typesystem::Type *t = Desugar(GetCppType(type));

  auto set_target = [&](cpp_typesystem::Type *pointee) {
    if (target_type && pointee)
      target_type->SetCompilerType(weak_from_this(),
                                   static_cast<opaque_compiler_type_t>(pointee));
  };

  // An Objective-C object is always accessed through a pointer (`Foo *` / `id`).
  // A pointer to an ObjC interface is a possible dynamic type when checking ObjC.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee =
        ptr->GetPointeeType() ? Desugar(ptr->GetPointeeType()) : nullptr;
    if (pointee) {
      if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(pointee)) {
        if (check_objc) {
          set_target(pointee);
          return true;
        }
        return false;
      }
      // `id` is modeled as a pointer to the opaque `objc_object` record.
      if (check_objc) {
        if (auto *rec = llvm::dyn_cast<cpp_typesystem::RecordType>(pointee)) {
          llvm::StringRef name = rec->GetName().GetName();
          if (name == "objc_object" || name == "objc_class") {
            set_target(pointee);
            return true;
          }
        }
      }
    }
  }

  // C++: a pointer or reference to a polymorphic class (one that -- or whose
  // base -- has a vtable) is a possible dynamic type. The object's vtable
  // pointer is followed to its RTTI to find the most-derived type (see the
  // Itanium ABI language runtime). Mirror TypeSystemClang::IsPossibleDynamicType:
  // also accept a pointer to `void` (an opaque pointer that may really point at
  // a polymorphic object). References only appear in C++, so they always imply
  // the C++ path.
  cpp_typesystem::Type *pointee = nullptr;
  bool is_reference = false;
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    pointee = ptr->GetPointeeType() ? Desugar(ptr->GetPointeeType()) : nullptr;
  } else if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    pointee = ref->GetPointeeType() ? Desugar(ref->GetPointeeType()) : nullptr;
    is_reference = true;
  }

  if (pointee && check_cplusplus) {
    // `void *` -- accept as a possible (watered-down) dynamic pointer, matching
    // TypeSystemClang. A reference can't be to void.
    if (!is_reference) {
      if (auto *bt = llvm::dyn_cast<cpp_typesystem::BuiltinType>(pointee)) {
        if (bt->GetEncoding() == lldb::eEncodingInvalid &&
            bt->GetName().GetName() == "void") {
          set_target(pointee);
          return true;
        }
      }
    }
    // A pointer/reference to a class: dynamic iff the class is polymorphic.
    // Complete the (possibly forward-declared) record first, since the vtable
    // fact is only known after completion -- this mirrors clang's
    // GetCompleteType() -> isDynamicClass() fallback.
    if (llvm::isa<cpp_typesystem::ClassType>(pointee)) {
      GetCompleteType(static_cast<opaque_compiler_type_t>(pointee));
      if (pointee->IsPolymorphic()) {
        set_target(pointee);
        return true;
      }
      return false;
    }
  }

  return false;
}

bool TypeSystemCpp::IsPointerType(opaque_compiler_type_t type,
                                  CompilerType *pointee_type) {
  if (pointee_type)
    pointee_type->Clear();
  auto *ptr = llvm::dyn_cast_or_null<cpp_typesystem::PointerType>(
      type ? Desugar(GetCppType(type)) : nullptr);
  if (!ptr)
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(ptr->GetPointeeType());
  return true;
}

bool TypeSystemCpp::IsScalarType(opaque_compiler_type_t type) {
  return type && (GetCppType(type)->GetTypeInfo() & eTypeIsScalar);
}

bool TypeSystemCpp::IsVoidType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // Void is the builtin with an invalid encoding spelled "void" (see
  // BuiltinTypes.cpp). Identify it by spelling so a void reached through another
  // Context (e.g. an expression result) is still recognized.
  auto *builtin =
      llvm::dyn_cast<cpp_typesystem::BuiltinType>(Desugar(GetCppType(type)));
  return builtin && builtin->GetEncoding() == lldb::eEncodingInvalid &&
         builtin->GetName().GetName() == "void";
}

bool TypeSystemCpp::CanPassInRegisters(const CompilerType &type) {
  return false;
}

bool TypeSystemCpp::SupportsLanguage(LanguageType language) {
  return Language::LanguageIsCFamily(language);
}

bool TypeSystemCpp::GetCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // See through typedefs/qualifiers: it is the underlying record that carries
  // completion state and is registered in the forward-declaration map.
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  if (!t)
    return false;
  if (t->IsComplete())
    return true;
  // Ask our SymbolFile to fill in the members from the debug info.
  if (SymbolFile *sym_file = GetSymbolFile()) {
    CompilerType ct = GetCompilerType(t);
    sym_file->CompleteType(ct);
  }
  // A record/enum that is still incomplete after asking the SymbolFile means
  // no definition could be found anywhere (e.g. -flimit-debug-info stripped it
  // from this module). We intentionally leave it incomplete rather than
  // forcefully completing it as an empty definition (unlike TypeSystemClang's
  // RequireCompleteType), but still record the same statistics signal
  // (GetHasForcefullyCompletedTypes) so `debugInfoHadIncompleteTypes` is
  // reported correctly.
  if (!t->IsComplete())
    m_has_forcefully_completed_types = true;
  return t->IsComplete();
}

bool TypeSystemCpp::IsForcefullyCompleted(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // TypeSystemCpp never force-completes a record as an empty definition the
  // way TypeSystemClang does (see GetCompleteType above) -- a record with no
  // definition anywhere in the debug info is left genuinely incomplete
  // instead. So this reports the closest equivalent: a record/enum that is
  // still incomplete after best-effort completion (i.e. -flimit-debug-info
  // stripped its only definition). ValueObject::GetSummaryAsCString uses this
  // to print "<incomplete type>" instead of trying to summarize a type it has
  // no members for. Scoped to RecordType (not desugared through references
  // etc.) to mirror TypeSystemClang::IsForcefullyCompleted, which only
  // recognizes a plain clang::RecordType.
  auto *record =
      llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(Desugar(GetCppType(type)));
  if (!record)
    return false;
  return !GetCompleteType(type);
}

void TypeSystemCpp::CompleteMemberFunctions(cpp_typesystem::Type *type) {
  if (!type)
    return;
  // See through typedefs/qualifiers to the underlying record, mirroring
  // GetCompleteType.
  auto *record =
      llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(Desugar(type));
  if (!record || record->AreMemberFunctionsParsed())
    return;
  // Member functions live on the completed record, and the DWARF parser learns
  // the record's defining DIE while completing it, so complete it first.
  GetCompleteType(record);
  if (auto *parser =
          llvm::dyn_cast_or_null<DWARFASTParserCpp>(GetDWARFParser()))
    parser->CompleteMemberFunctionsFromDWARF(*record);
}

std::vector<CompilerDeclContext>
TypeSystemCpp::GetUsingDirectiveNamespaces(Block &block) {
  std::vector<CompilerDeclContext> namespaces;
  auto *parser = llvm::dyn_cast_or_null<DWARFASTParserCpp>(GetDWARFParser());
  if (!parser)
    return namespaces;
  auto *dwarf = llvm::dyn_cast_or_null<plugin::dwarf::SymbolFileDWARF>(
      block.GetSymbolFile());
  if (!dwarf)
    return namespaces;
  plugin::dwarf::DWARFDIE block_die = dwarf->GetDIE(block.GetID());
  if (!block_die)
    return namespaces;
  parser->CollectUsingDirectiveNamespaces(block_die, namespaces);
  return namespaces;
}

std::vector<std::pair<ConstString, CompilerDeclContext>>
TypeSystemCpp::GetUsingDeclarations(Block &block) {
  std::vector<std::pair<ConstString, CompilerDeclContext>> decls;
  auto *parser = llvm::dyn_cast_or_null<DWARFASTParserCpp>(GetDWARFParser());
  if (!parser)
    return decls;
  auto *dwarf = llvm::dyn_cast_or_null<plugin::dwarf::SymbolFileDWARF>(
      block.GetSymbolFile());
  if (!dwarf)
    return decls;
  plugin::dwarf::DWARFDIE block_die = dwarf->GetDIE(block.GetID());
  if (!block_die)
    return decls;
  parser->CollectUsingDeclarations(block_die, decls);
  return decls;
}

CompilerType TypeSystemCpp::GetOwningClassForFunction(Block &function_block) {
  auto *parser = llvm::dyn_cast_or_null<DWARFASTParserCpp>(GetDWARFParser());
  if (!parser)
    return CompilerType();
  auto *dwarf = llvm::dyn_cast_or_null<plugin::dwarf::SymbolFileDWARF>(
      function_block.GetSymbolFile());
  if (!dwarf)
    return CompilerType();
  plugin::dwarf::DWARFDIE block_die = dwarf->GetDIE(function_block.GetID());
  if (!block_die)
    return CompilerType();
  return parser->GetOwningClassForFunctionFromDWARF(block_die);
}

uint32_t TypeSystemCpp::GetPointerByteSize() {
  return m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size;
}

CompilerType TypeSystemCpp::GetPointerDiffType(bool is_signed) {
  // The result of pointer subtraction. Clang spells this builtin `__ptrdiff_t`
  // (and its unsigned counterpart), sized as a pointer. Model it as a bespoke
  // builtin of pointer width so value objects created from it show that name.
  const uint64_t byte_size =
      m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size;
  cpp_typesystem::Builder builder(*this);
  if (is_signed)
    return builder.GetBuiltinType("__ptrdiff_t", byte_size,
                                  lldb::eEncodingSint, lldb::eFormatDecimal);
  return builder.GetBuiltinType("__ptrdiff_t unsigned", byte_size,
                                lldb::eEncodingUint, lldb::eFormatUnsigned);
}

unsigned TypeSystemCpp::GetPtrAuthKey(opaque_compiler_type_t type) {
  if (auto *pa = FindPtrAuthType(type))
    return pa->GetKey();
  return 0;
}

unsigned TypeSystemCpp::GetPtrAuthDiscriminator(opaque_compiler_type_t type) {
  if (auto *pa = FindPtrAuthType(type))
    return pa->GetExtraDiscriminator();
  return 0;
}

bool TypeSystemCpp::GetPtrAuthAddressDiversity(opaque_compiler_type_t type) {
  if (auto *pa = FindPtrAuthType(type))
    return pa->IsAddressDiscriminated();
  return false;
}

bool TypeSystemCpp::HasPointerAuthQualifier(opaque_compiler_type_t type) {
  return FindPtrAuthType(type) != nullptr;
}

// If `t` is an incomplete record whose spelling carries template arguments
// (contains `<`), complete it so its modeled template arguments are available.
// Building the reconstructed display name needs those arguments; without them
// the raw DWARF spelling is used verbatim, which renders enum-typed non-type
// arguments as `(EnumType)0` rather than the enumerator name. Recurses into
// type-kind template arguments (`Foo<Bar<1L>>`): reconstructing `Foo`'s name
// needs `Bar`'s own arguments too, since `Bar<1L>`'s spelling is embedded in
// `Foo`'s DWARF name.
void TypeSystemCpp::CompleteTemplateInstantiationForName(
    cpp_typesystem::Type *t) {
  auto *rec = llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(t);
  if (!rec || !rec->GetName().GetName().contains('<'))
    return;
  if (!rec->IsComplete())
    GetCompleteType(rec);
  for (uint32_t i = 0, e = rec->GetNumTemplateArguments(); i != e; ++i) {
    const cpp_typesystem::TemplateArgument *arg =
        rec->GetTemplateArgumentAtIndex(i);
    if (arg->kind == lldb::eTemplateArgumentKindType)
      CompleteTemplateInstantiationForName(arg->type.Get());
  }
}

ConstString TypeSystemCpp::GetTypeName(opaque_compiler_type_t type,
                                       bool BaseOnly) {
  if (!type)
    return ConstString();
  cpp_typesystem::Type *t = GetCppType(type);
  // Strip elaborated display sugar (e.g. the `::` of `::Struct`, or template
  // spelling sugar) from the canonical name: like clang's RemoveWrappingTypes,
  // the source spelling only affects the *display* name, so a formatter keyed on
  // `Struct` still matches a `::Struct`-spelled value. Typedefs are kept (they
  // are meaningful, distinct names).
  while (auto *el = llvm::dyn_cast<cpp_typesystem::ElaboratedType>(t))
    t = el->GetUnderlyingType();
  // A class-template instantiation's display name is reconstructed from its
  // modeled template arguments (so an enum-typed non-type argument prints as
  // `EnumType::Member` rather than the DWARF producer's `(EnumType)0`). Those
  // arguments only exist once the record is completed, so complete it now.
  CompleteTemplateInstantiationForName(t);
  // Arrays have no name of their own; build "<element>[<count>]". For a
  // multidimensional array the nesting is outermost-dimension first, so peel
  // the whole chain and print the dimensions in source order after the
  // innermost element's name.
  if (llvm::isa<cpp_typesystem::ArrayType>(t)) {
    // A vector type (DW_AT_GNU_vector) is spelled like clang's
    // `<element> __attribute__((ext_vector_type(N)))` rather than as an array
    // (`<element>[N]`), so it doesn't get matched by array/char-array
    // formatters (matching TypeSystemClang's type name for vectors).
    if (auto *vec = llvm::dyn_cast<cpp_typesystem::ArrayType>(t);
        vec && vec->IsVector()) {
      std::string element_name =
          GetTypeName(vec->GetElementType(), BaseOnly).GetStringRef().str();
      uint64_t n = vec->GetNumElements().value_or(0);
      return ConstString(
          llvm::formatv("{0} __attribute__((ext_vector_type({1})))",
                        element_name, n)
              .str());
    }
    std::string dims;
    cpp_typesystem::Type *cur = t;
    while (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(cur)) {
      if (std::optional<uint64_t> num_elements = array->GetNumElements())
        dims += llvm::formatv("[{0}]", *num_elements).str();
      else
        dims += "[]";
      cur = array->GetElementType();
    }
    std::string element_name = GetTypeName(cur, BaseOnly).GetStringRef().str();
    return ConstString(element_name + dims);
  }
  // Function types and pointers/references to them need C declarator syntax
  // (`int (*)(const char *)`); BuildDisplayName already produces it.
  if (llvm::isa<cpp_typesystem::FunctionType>(t))
    return ConstString(BuildDisplayName(t));
  if (llvm::isa<cpp_typesystem::ComplexType>(t))
    return ConstString(BuildDisplayName(t));
  // Pointers have no name of their own either; build "<pointee> *".
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (llvm::isa_and_nonnull<cpp_typesystem::FunctionType>(pointee))
      return ConstString(BuildDisplayName(t));
    std::string pointee_name =
        pointee ? GetTypeName(pointee, BaseOnly).GetStringRef().str() : "void";
    // Clang omits the space before '*' when the pointee already ends in a
    // pointer/reference sigil ("void **", "int *&"), so keep the two tight.
    const bool tight =
        !pointee_name.empty() &&
        (pointee_name.back() == '*' || pointee_name.back() == '&');
    return ConstString(pointee_name + (tight ? "*" : " *"));
  }
  // References likewise: "<pointee> &" or "<pointee> &&".
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (llvm::isa_and_nonnull<cpp_typesystem::FunctionType>(pointee))
      return ConstString(BuildDisplayName(t));
    std::string pointee_name =
        pointee ? GetTypeName(pointee, BaseOnly).GetStringRef().str() : "void";
    const bool tight =
        !pointee_name.empty() &&
        (pointee_name.back() == '*' || pointee_name.back() == '&');
    llvm::StringRef sigil = ref->IsRValue() ? "&&" : "&";
    return ConstString(pointee_name + (tight ? sigil.str() : (" " + sigil.str())));
  }
  // cv-qualified types render as "const"/"volatile" prefixing the unqualified
  // name. Matching TypeSystemClang's GetTypeName: the cv-qualifiers are only
  // spelled for types whose name is printed from the QualType (builtins,
  // pointers, references, ...). For a tag type (record/enum) or a typedef the
  // name is taken from the underlying decl, which carries no qualifiers, so
  // `const Enum` prints as `Enum` (while `const int` stays `const int`).
  if (auto *cv = llvm::dyn_cast<cpp_typesystem::CVQualifiedType>(t)) {
    std::string underlying_name =
        cv->GetUnderlyingType() ? GetTypeName(cv->GetUnderlyingType(), BaseOnly)
                                      .GetStringRef()
                                      .str()
                                : "";
    // Look through display/cv sugar to the leaf type to decide whether the
    // qualifiers are spelled.
    cpp_typesystem::Type *leaf = cv->GetUnderlyingType();
    while (leaf) {
      if (auto *el = llvm::dyn_cast<cpp_typesystem::ElaboratedType>(leaf))
        leaf = el->GetUnderlyingType();
      else if (auto *inner =
                   llvm::dyn_cast<cpp_typesystem::CVQualifiedType>(leaf))
        leaf = inner->GetUnderlyingType();
      else
        break;
    }
    const bool drop_qualifiers =
        leaf && (llvm::isa<cpp_typesystem::RecordType>(leaf) ||
                 llvm::isa<cpp_typesystem::EnumType>(leaf) ||
                 llvm::isa<cpp_typesystem::TypedefType>(leaf));
    if (drop_qualifiers)
      return ConstString(underlying_name);
    std::string result;
    if (cv->IsConst())
      result += "const ";
    if (cv->IsVolatile())
      result += "volatile ";
    result += underlying_name;
    return ConstString(result);
  }
  if (auto *pa = llvm::dyn_cast<cpp_typesystem::PtrAuthType>(t)) {
    std::string underlying =
        GetTypeName(pa->GetUnderlyingType(), BaseOnly).GetStringRef().str();
    std::string qualifier = BuildPtrAuthQualifier(pa);
    // Suffix on a pointer (`int *__ptrauth(...)`), prefix otherwise
    // (`__ptrauth(...) intp`), matching clang's spelling.
    if (llvm::isa_and_nonnull<cpp_typesystem::PointerType>(
            pa->GetUnderlyingType()))
      return ConstString(underlying + qualifier);
    return ConstString(qualifier + " " + underlying);
  }
  // Named leaf type (record/typedef/enum/builtin). `BaseOnly` asks for the
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
  if (auto *rec = llvm::dyn_cast<cpp_typesystem::RecordType>(t)) {
    if (NeedsTemplateNameReconstruction(rec)) {
      llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
      std::string base = unqualified.substr(0, unqualified.find('<')).str();
      std::string args = BuildTemplateArgList(rec, /*hide_default_args=*/false,
                                              /*keep_inline_namespaces=*/true);
      if (BaseOnly)
        return ConstString(base + args);
      // Fully-qualified: prefix the enclosing namespace/class scopes, keeping
      // inline namespaces (unlike the display name) since this is the raw
      // spelling data formatters key on.
      std::string result;
      AppendQualifiedNamespacePrefix(t->GetDeclContext(), result);
      AppendClassScopePrefix(t->GetName().GetName(), t->GetDeclContext(),
                             result);
      return ConstString(result + base + args);
    }
  }
  if (BaseOnly) {
    llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
    if (!unqualified.empty())
      return ConstString(unqualified);
  }
  // An unnamed record/enum has no spelling in the debug info; render it as
  // "(unnamed struct)" etc. to match clang / TypeSystemClang. An anonymous
  // struct/union additionally gets its enclosing record's name prefixed (e.g.
  // "MySock::(anonymous union)"); see AppendAnonymousParentPrefix. Otherwise
  // (e.g. a lambda closure type) qualify by the enclosing namespace chain
  // instead, same as the named-type case above (keeping inline namespaces,
  // matching the raw-spelling convention this function otherwise uses).
  // `BaseOnly` asks for the unqualified spelling, so skip both prefixes in
  // that case.
  if (std::string unnamed = BuildUnnamedTagName(t); !unnamed.empty()) {
    std::string result;
    if (!BaseOnly) {
      AppendAnonymousParentPrefix(t, result);
      if (result.empty())
        AppendUnnamedTagNamespacePrefix(t->GetDeclContext(), result,
                                        /*keep_inline_namespaces=*/true);
    }
    return ConstString(result + unnamed);
  }
  return ConstString(t->GetName().GetName());
}

ConstString TypeSystemCpp::GetDisplayTypeName(opaque_compiler_type_t type) {
  if (!type)
    return ConstString();
  cpp_typesystem::Type *t = GetCppType(type);
  CompleteTemplateInstantiationForName(t);
  return ConstString(BuildDisplayName(t));
}

uint32_t
TypeSystemCpp::GetTypeInfo(opaque_compiler_type_t type,
                           CompilerType *pointee_or_element_compiler_type) {
  if (pointee_or_element_compiler_type)
    pointee_or_element_compiler_type->Clear();
  if (!type)
    return 0;
  cpp_typesystem::Type *t = GetCppType(type);
  // Hand back the element/pointee type when asked; callers such as
  // ValueObject::GetPointeeData rely on it to know how to read array elements
  // or dereference pointers/references.
  if (pointee_or_element_compiler_type) {
    cpp_typesystem::Type *inner = nullptr;
    if (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(Desugar(t)))
      inner = array->GetElementType();
    else if (auto *ptr =
                 llvm::dyn_cast<cpp_typesystem::PointerType>(Desugar(t)))
      inner = ptr->GetPointeeType();
    else if (auto *ref =
                 llvm::dyn_cast<cpp_typesystem::ReferenceType>(Desugar(t)))
      inner = ref->GetPointeeType();
    if (inner)
      *pointee_or_element_compiler_type = GetCompilerType(inner);
  }
  return t->GetTypeInfo();
}

LanguageType TypeSystemCpp::GetMinimumLanguage(opaque_compiler_type_t type) {
  // TypeSystemCpp models C/C++/Objective-C++ types. Reporting C++ is what puts
  // types in the C++ formatter category, so that e.g. the libc++ container
  // data formatters are consulted (see FormatManager::GetCandidateLanguages).
  //
  // Exception: a pointer to a plain scalar/enum (e.g. `int *`, `enum E *`) is a
  // C construct. Reporting it as C++ makes CPlusPlusLanguage::IsNilReference
  // treat a null such pointer as a nil object reference and print it as "NULL"
  // instead of its address; TypeSystemClang reports C for these, printing 0x0.
  // Pointers to records keep reporting C++ so class data formatters still fire.
  if (type) {
    cpp_typesystem::Type *t = Desugar(GetCppType(type));
    // Resolve through a reference to what it refers to first, matching
    // TypeSystemClang's GetCanonicalQualType(type).getNonReferenceType():
    // `Shape &` must report C++ (so e.g. GetVTable's language-runtime lookup
    // and class formatters fire) exactly like `Shape` does, not fall through
    // to the "plain non-record type" C default below (a reference is never
    // itself the "plain scalar" case that default exists for).
    if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t))
      if (cpp_typesystem::Type *referent = ref->GetPointeeType())
        t = Desugar(referent);
    // An Objective-C interface (or a pointer to one, i.e. an ObjC object like
    // `NSObject *`) is an Objective-C construct. Reporting ObjC is what routes
    // it to the ObjC language runtime for dynamic-type resolution (see
    // ValueObjectDynamicValue::UpdateValue).
    if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(t))
      return eLanguageTypeObjC;
    if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
      cpp_typesystem::Type *pointee = ptr->GetPointeeType();
      if (pointee &&
          llvm::isa<cpp_typesystem::ObjCInterfaceType>(Desugar(pointee)))
        return eLanguageTypeObjC;
      if (pointee) {
        // `id` / `Class` are modeled as a pointer to the opaque
        // `objc_object` / `objc_class` record (see IsPossibleDynamicType);
        // recognize that idiom too so e.g. an `id`-typed container element
        // still gets routed to the ObjC runtime for dynamic-type resolution.
        if (auto *rec =
                llvm::dyn_cast<cpp_typesystem::RecordType>(Desugar(pointee))) {
          llvm::StringRef name = rec->GetName().GetName();
          if (name == "objc_object" || name == "objc_class")
            return eLanguageTypeObjC;
        }
      }
      if (!pointee ||
          !llvm::isa<cpp_typesystem::RecordType>(Desugar(pointee)))
        return eLanguageTypeC;
    } else if (!llvm::isa<cpp_typesystem::RecordType>(t)) {
      // A plain (non-pointer, non-record, non-ObjC) type -- e.g. `int` -- is
      // a C construct, matching TypeSystemClang's default fallthrough
      // (eLanguageTypeC) for anything that isn't a CXXRecordDecl/ObjC
      // construct/pointer-to-record. Records themselves keep falling through
      // to eLanguageTypeC_plus_plus below so C++ class formatters still fire.
      return eLanguageTypeC;
    }
  }
  return eLanguageTypeC_plus_plus;
}

TypeClass TypeSystemCpp::GetTypeClass(opaque_compiler_type_t type) {
  if (!type)
    return eTypeClassInvalid;
  return GetCppType(type)->GetTypeClass();
}

CompilerType
TypeSystemCpp::GetArrayElementType(opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) {
  if (!type)
    return CompilerType();
  if (auto *array =
          llvm::dyn_cast<cpp_typesystem::ArrayType>(Desugar(GetCppType(type))))
    return GetCompilerType(array->GetElementType());
  return CompilerType();
}

CompilerType TypeSystemCpp::GetArrayType(opaque_compiler_type_t type,
                                         uint64_t size) {
  if (!type)
    return CompilerType();
  // Build an array of `size` elements of `type` (size 0 => unbounded). Used by
  // the formatter matcher to form a typedef-stripped array candidate name (e.g.
  // `MCHAR[5]` -> `char[5]`), so char-array-of-typedef matches the char[] string
  // summary.
  return cpp_typesystem::Builder(*this).CreateArrayType(
      GetCompilerType(GetCppType(type)),
      size ? std::optional<uint64_t>(size)
           : std::optional<uint64_t>(std::nullopt));
}

CompilerType TypeSystemCpp::GetCanonicalType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  // The canonical type is the type with all typedef/cv sugar stripped.
  return GetCompilerType(Desugar(GetCppType(type)));
}

CompilerType
TypeSystemCpp::GetEnumerationIntegerType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  if (auto *enum_type =
          llvm::dyn_cast<cpp_typesystem::EnumType>(Desugar(GetCppType(type))))
    return GetCompilerType(enum_type->GetUnderlyingType());
  return CompilerType();
}

void TypeSystemCpp::ForEachEnumerator(
    opaque_compiler_type_t type,
    std::function<bool(const CompilerType &integer_type, ConstString name,
                       const llvm::APSInt &value)> const &callback) {
  if (!type)
    return;
  auto *enum_type =
      llvm::dyn_cast<cpp_typesystem::EnumType>(Desugar(GetCppType(type)));
  if (!enum_type)
    return;

  CompilerType integer_type = GetCompilerType(enum_type->GetUnderlyingType());
  const bool is_signed = enum_type->IsSigned();
  // Enumerator values are stored as raw bits; recover their APSInt using the
  // underlying type's width so signed values sign-extend correctly.
  unsigned bit_width = 64;
  if (std::optional<uint64_t> byte_size =
          llvm::expectedToOptional(integer_type.GetByteSize(nullptr)))
    bit_width = static_cast<unsigned>(*byte_size * 8);

  for (const cpp_typesystem::Enumerator &enumerator :
       enum_type->GetEnumerators()) {
    llvm::APSInt value(llvm::APInt(bit_width, enumerator.value, is_signed),
                       !is_signed);
    if (!callback(integer_type, ConstString(enumerator.name.GetName()), value))
      break;
  }
}

int TypeSystemCpp::GetFunctionArgumentCount(opaque_compiler_type_t type) {
  if (!type)
    return -1;
  if (auto *fn = llvm::dyn_cast<cpp_typesystem::FunctionType>(
          Desugar(GetCppType(type))))
    return static_cast<int>(fn->GetNumParameters());
  return -1;
}

CompilerType
TypeSystemCpp::GetFunctionArgumentTypeAtIndex(opaque_compiler_type_t type,
                                              size_t idx) {
  return GetFunctionArgumentAtIndex(type, idx);
}

CompilerType TypeSystemCpp::GetFunctionReturnType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  if (auto *fn = llvm::dyn_cast<cpp_typesystem::FunctionType>(
          Desugar(GetCppType(type))))
    return GetCompilerType(fn->GetReturnType());
  return CompilerType();
}

size_t TypeSystemCpp::GetNumMemberFunctions(opaque_compiler_type_t type) {
  if (!type)
    return 0;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // Member functions of a record; ObjC methods of an interface (reached through
  // a pointer, as always for ObjC). They are parsed lazily.
  cpp_typesystem::Type *bearer = GetObjCBaseClassBearingType(type);
  if (auto *iface = llvm::dyn_cast<cpp_typesystem::ObjCInterfaceType>(bearer)) {
    CompleteMemberFunctions(iface);
    return iface->GetNumObjCMethods();
  }
  if (auto *record = llvm::dyn_cast<cpp_typesystem::RecordType>(t)) {
    CompleteMemberFunctions(record);
    return record->GetNumMemberFunctions();
  }
  return 0;
}

TypeMemberFunctionImpl
TypeSystemCpp::GetMemberFunctionAtIndex(opaque_compiler_type_t type,
                                        size_t idx) {
  if (!type)
    return TypeMemberFunctionImpl();
  cpp_typesystem::Type *t = Desugar(GetCppType(type));

  // Objective-C methods (reached through the interface, possibly via a pointer).
  cpp_typesystem::Type *bearer = GetObjCBaseClassBearingType(type);
  if (auto *iface = llvm::dyn_cast<cpp_typesystem::ObjCInterfaceType>(bearer)) {
    CompleteMemberFunctions(iface);
    const cpp_typesystem::ObjCMethod *method = iface->GetObjCMethodAtIndex(idx);
    if (!method)
      return TypeMemberFunctionImpl();
    // Clang names the member function by the method's selector (e.g. `foo:` /
    // `init`). The stored name is the full `-[Class sel:]`; recover the
    // selector from it.
    std::string name = method->name.GetName().str();
    if (std::optional<const ObjCLanguage::ObjCMethodName> parsed =
            ObjCLanguage::ObjCMethodName::Create(name, /*strict=*/false))
      name = parsed->GetSelector().str();
    MemberFunctionKind kind = method->is_class_method
                                  ? lldb::eMemberFunctionKindStaticMethod
                                  : lldb::eMemberFunctionKindInstanceMethod;
    return TypeMemberFunctionImpl(GetCompilerType(method->type.Get()),
                                  CompilerDecl(), name, kind);
  }

  auto *record = llvm::dyn_cast<cpp_typesystem::RecordType>(t);
  if (!record)
    return TypeMemberFunctionImpl();
  CompleteMemberFunctions(record);
  const cpp_typesystem::MemberFunction *method =
      record->GetMemberFunctionAtIndex(idx);
  if (!method)
    return TypeMemberFunctionImpl();

  MemberFunctionKind kind;
  switch (method->kind) {
  case cpp_typesystem::MemberFunctionKind::Constructor:
    kind = lldb::eMemberFunctionKindConstructor;
    break;
  case cpp_typesystem::MemberFunctionKind::Destructor:
    kind = lldb::eMemberFunctionKindDestructor;
    break;
  case cpp_typesystem::MemberFunctionKind::Method:
    kind = method->is_static ? lldb::eMemberFunctionKindStaticMethod
                             : lldb::eMemberFunctionKindInstanceMethod;
    break;
  }
  // Hand out a tagged Decl so GetMangledName/GetDemangledName can recover the
  // linkage name; the (return/argument) types come from the function type.
  CompilerDecl decl(this,
                    const_cast<cpp_typesystem::Decl *>(m_context.GetOrCreateDecl(
                        cpp_typesystem::Decl::Kind::MemberFunction, method)));
  return TypeMemberFunctionImpl(GetCompilerType(method->type.Get()), decl,
                                method->name.GetName().str(), kind);
}

CompilerType TypeSystemCpp::GetPointeeType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  cpp_typesystem::Type *desugared = Desugar(GetCppType(type));
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(desugared)) {
    // A null pointee models `void *` (see PointerType). Surface the canonical
    // void builtin so callers (e.g. CompilerType::IsPointerToVoid) can identify
    // it via GetBasicTypeEnumeration().
    if (cpp_typesystem::Type *pointee = ptr->GetPointeeType())
      return GetCompilerType(pointee);
    return GetCompilerType(
        m_context.GetBuiltinType(cpp_typesystem::BuiltinKind::Void));
  }
  // A reference's "pointee" is the referenced type, matching
  // TypeSystemClang::GetPointeeType (clang::Type::getPointeeType() answers
  // both pointers and references). Unlike a pointer, a reference always
  // refers to a concrete type (there is no `void &`), so no void fallback is
  // needed here.
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(desugared))
    if (cpp_typesystem::Type *referent = ref->GetPointeeType())
      return GetCompilerType(referent);
  return CompilerType();
}

CompilerType TypeSystemCpp::GetPointerType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  return cpp_typesystem::Builder(*this).CreatePointerType(
      GetCompilerType(GetCppType(type)));
}

CompilerType
TypeSystemCpp::GetLValueReferenceType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  return cpp_typesystem::Builder(*this).CreateReferenceType(
      GetCompilerType(GetCppType(type)), /*is_rvalue=*/false);
}

CompilerType
TypeSystemCpp::GetRValueReferenceType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  return cpp_typesystem::Builder(*this).CreateReferenceType(
      GetCompilerType(GetCppType(type)), /*is_rvalue=*/true);
}

CompilerType
TypeSystemCpp::GetTypeForFormatters(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  // Data formatters match on type name and are meant to be cv-agnostic (e.g.
  // the C-string summary is registered for `char *`, and clang strips the
  // cv-qualifiers so `const char *` matches too). Peel any top-level
  // cv-qualifier sugar, and -- because a `char *` summary keys off the pointer
  // type's spelling -- also rebuild a pointer whose pointee is cv-qualified as
  // a pointer to the unqualified pointee. Typedefs are preserved (they are
  // meaningful to formatters).
  auto strip_cv = [](cpp_typesystem::Type *t) -> cpp_typesystem::Type * {
    while (auto *cv = llvm::dyn_cast_or_null<cpp_typesystem::CVQualifiedType>(t))
      t = cv->GetUnderlyingType();
    return t;
  };

  cpp_typesystem::Type *t = strip_cv(GetCppType(type));
  if (!t)
    return CompilerType();

  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    cpp_typesystem::Type *stripped = strip_cv(pointee);
    if (stripped != pointee && stripped)
      return cpp_typesystem::Builder(*this).CreatePointerType(
          GetCompilerType(stripped));
  }
  // The C-string array summary keys off an array type's spelling
  // (`char[N]` / `unsigned char[N]`), which clang produces by stripping the
  // element's cv-qualifiers. An array of `const char` is modeled here with the
  // cv-qualifier on the element (not the array), so rebuild the array with the
  // unqualified element so `const char[N]` also matches.
  if (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(t)) {
    cpp_typesystem::Type *element = array->GetElementType();
    cpp_typesystem::Type *stripped = strip_cv(element);
    if (stripped != element && stripped)
      return cpp_typesystem::Builder(*this).CreateArrayType(
          GetCompilerType(stripped), array->GetNumElements());
  }
  return GetCompilerType(t);
}

const llvm::fltSemantics &TypeSystemCpp::GetFloatTypeSemantics(size_t byte_size,
                                                               Format format) {
  return m_context.GetLanguageOpts().GetFloatTypeSemantics(byte_size, format);
}

llvm::Expected<uint64_t>
TypeSystemCpp::GetBitSize(opaque_compiler_type_t type,
                          ExecutionContextScope *exe_scope) {
  if (!type)
    return llvm::createStringError("invalid type");
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // An Objective-C class's DWARF-recorded byte size is a compile-time constant
  // baked into whichever module's debug info produced it. It can be smaller
  // than the class's true instance size when ivars are added downstream of
  // that module -- e.g. a class extension in a different image adds a hidden
  // ivar to a superclass (see the hidden-ivars test): the subclass's own
  // DW_AT_byte_size, emitted by a compile that only saw the superclass's
  // public ivars, doesn't leave room for the hidden one. Query the ObjC
  // runtime's authoritative instance size first, mirroring the ivar-offset
  // override in GetChildCompilerTypeAtIndex -- getting this wrong silently
  // undersizes the buffer used to materialize a whole-object expression
  // result (e.g. `*k`), truncating trailing ivars.
  if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(t) && exe_scope) {
    if (lldb::ProcessSP process_sp = exe_scope->CalculateProcess()) {
      if (ObjCLanguageRuntime *objc_runtime =
              ObjCLanguageRuntime::Get(*process_sp)) {
        ConstString class_name(t->GetName().GetName());
        if (ObjCLanguageRuntime::ClassDescriptorSP descriptor =
                objc_runtime->GetClassDescriptorFromClassName(class_name)) {
          uint64_t instance_size = descriptor->GetInstanceSize();
          if (instance_size != 0)
            return instance_size * 8;
        }
      }
    }
  }
  if (std::optional<uint64_t> byte_size = t->GetByteSize())
    return *byte_size * 8;
  // Function types have no storage of their own. Matching TypeSystemClang
  // (clang models function types with a type size of 0), report a bit size of
  // 0 rather than an error. This keeps the dereferenced-value child of a
  // function pointer/reference from surfacing a size error (or a spurious byte
  // read) as its summary -- a zero-sized value simply has no value string.
  if (llvm::isa<cpp_typesystem::FunctionType>(t))
    return 0;
  return llvm::createStringError("TypeSystemCpp::GetBitSize: unknown size");
}

Encoding TypeSystemCpp::GetEncoding(opaque_compiler_type_t type) {
  if (!type)
    return eEncodingInvalid;
  return GetCppType(type)->GetEncoding();
}

Format TypeSystemCpp::GetFormat(opaque_compiler_type_t type) {
  if (!type)
    return eFormatDefault;
  return GetCppType(type)->GetFormat();
}

CompilerType TypeSystemCpp::RealizeObjCEncoding(cpp_typesystem::Builder &builder,
                                                llvm::StringRef &enc) {
  // Skip leading method/ivar qualifier characters (const, in/out, byref, ...).
  while (!enc.empty() && llvm::StringRef("rnNoRVA").contains(enc.front()))
    enc = enc.drop_front();
  if (enc.empty())
    return CompilerType();
  const char c = enc.front();
  enc = enc.drop_front();
  auto builtin = [&](const char *name, uint64_t size, lldb::Encoding e,
                     lldb::Format f) {
    return builder.GetBuiltinType(name, size, e, f);
  };
  switch (c) {
  case 'c':
    return builtin("char", 1, lldb::eEncodingSint, lldb::eFormatChar);
  case 'C':
    return builtin("unsigned char", 1, lldb::eEncodingUint, lldb::eFormatChar);
  case 'B':
    return builtin("bool", 1, lldb::eEncodingUint, lldb::eFormatBoolean);
  case 's':
    return builtin("short", 2, lldb::eEncodingSint, lldb::eFormatDecimal);
  case 'S':
    return builtin("unsigned short", 2, lldb::eEncodingUint,
                   lldb::eFormatUnsigned);
  case 'i':
    return builtin("int", 4, lldb::eEncodingSint, lldb::eFormatDecimal);
  case 'I':
    return builtin("unsigned int", 4, lldb::eEncodingUint,
                   lldb::eFormatUnsigned);
  case 'l':
    return builtin("long", 8, lldb::eEncodingSint, lldb::eFormatDecimal);
  case 'L':
    return builtin("unsigned long", 8, lldb::eEncodingUint,
                   lldb::eFormatUnsigned);
  case 'q':
    return builtin("long long", 8, lldb::eEncodingSint, lldb::eFormatDecimal);
  case 'Q':
    return builtin("unsigned long long", 8, lldb::eEncodingUint,
                   lldb::eFormatUnsigned);
  case 'f':
    return builtin("float", 4, lldb::eEncodingIEEE754, lldb::eFormatFloat);
  case 'd':
    return builtin("double", 8, lldb::eEncodingIEEE754, lldb::eFormatFloat);
  case 'v':
    return builder.GetVoidType();
  case '*': // char *
    return builder.CreatePointerType(
        builtin("char", 1, lldb::eEncodingSint, lldb::eFormatChar));
  case '@': // id
    // Named (not just an anonymous pointer) so ClangASTGenerator::GenerateType
    // recognizes it by its typedef name and maps it to clang's builtin `id`
    // (see the comment there): a method whose return/parameter type is `id`
    // needs real ObjC `id` semantics (e.g. implicit conversions to/from any
    // object pointer) for Sema to accept it, not just an opaque `void *`.
    return builder.CreateTypedefType(
        "id", builder.CreatePointerType(CompilerType()));
  case '#': // Class
    return builder.CreateTypedefType(
        "Class", builder.CreatePointerType(CompilerType()));
  case ':': // SEL
    return builder.CreateTypedefType(
        "SEL", builder.CreatePointerType(CompilerType()));
  case '^': { // pointer to the following encoding
    CompilerType pointee = RealizeObjCEncoding(builder, enc);
    return builder.CreatePointerType(pointee);
  }
  default:
    return CompilerType();
  }
}

namespace {
/// Splits a runtime method type-encoding string (e.g. "i16@0:8") into its
/// per-argument type substrings, discarding the stack-offset digits that
/// follow each one. Mirrors AppleObjCDeclVendor.cpp's ObjCRuntimeMethodType,
/// which does the same for building a clang::ObjCMethodDecl; this is a
/// faithful port to avoid subtly diverging on the (undocumented, encoding-
/// specific) parsing rules -- e.g. that a digit inside a `{...}`/`[...]`/
/// `(...)` group is part of the type (an array/bitfield count), not the
/// argument's stack offset, so brace-depth tracking is required to tell them
/// apart.
class ObjCRuntimeMethodSignature {
public:
  explicit ObjCRuntimeMethodSignature(const char *types) {
    if (!types)
      return;
    const char *cursor = types;
    enum State { Start, InType, InPos } state = Start;
    const char *type_start = nullptr;
    int brace_depth = 0;
    uint32_t steps_left = 256;

    while (true) {
      if (--steps_left == 0)
        return;
      switch (state) {
      case Start:
        if (*cursor == '\0') {
          m_is_valid = true;
          return;
        }
        if (llvm::isDigit(*cursor))
          return; // A type-encoding can't start with a digit.
        state = InType;
        type_start = cursor;
        break;
      case InType:
        switch (*cursor) {
        case '\0':
          return; // A type must be followed by its stack-offset digits.
        case '[':
        case '{':
        case '(':
          ++brace_depth;
          ++cursor;
          break;
        case ']':
        case '}':
        case ')':
          if (!brace_depth)
            return;
          --brace_depth;
          ++cursor;
          break;
        default:
          if (llvm::isDigit(*cursor) && !brace_depth) {
            m_types.push_back(std::string(type_start, cursor - type_start));
            type_start = nullptr;
            state = InPos;
          } else {
            ++cursor;
          }
          break;
        }
        break;
      case InPos:
        if (*cursor == '\0') {
          m_is_valid = true;
          return;
        }
        if (llvm::isDigit(*cursor)) {
          ++cursor;
        } else {
          state = InType;
          type_start = cursor;
        }
        break;
      }
    }
  }

  explicit operator bool() const { return m_is_valid; }
  size_t GetNumTypes() const { return m_types.size(); }
  llvm::StringRef GetTypeAtIndex(size_t idx) const { return m_types[idx]; }

private:
  std::vector<std::string> m_types;
  bool m_is_valid = false;
};
} // namespace

void TypeSystemCpp::AddRuntimeObjCMethod(cpp_typesystem::Builder &builder,
                                         cpp_typesystem::ObjCInterfaceType &iface,
                                         llvm::StringRef class_name,
                                         const char *selector,
                                         const char *type_encoding,
                                         bool is_class_method) {
  if (!selector || !selector[0])
    return;
  ObjCRuntimeMethodSignature sig(type_encoding);
  // A method's encoding is at least [return, self, _cmd]; reject anything
  // shorter as corrupt runtime metadata.
  if (!sig || sig.GetNumTypes() < 3)
    return;

  llvm::StringRef return_enc = sig.GetTypeAtIndex(0);
  CompilerType return_type = RealizeObjCEncoding(builder, return_enc);
  // A return/parameter type RealizeObjCEncoding can't decode (e.g. a
  // struct-by-value return/argument) means the method can't be fully typed;
  // drop it rather than synthesize a signature Sema would type-check
  // incorrectly. Mirrors AppleObjCDeclVendor::ObjCRuntimeMethodType::
  // BuildMethod, which likewise gives up on such a method instead of
  // approximating it.
  if (!return_type)
    return;

  // Indices 1 and 2 are the implicit self/_cmd parameters, which
  // cpp_typesystem::ObjCMethod's FunctionType does not carry (matching the
  // DWARF path -- see DWARFASTParserCpp::CompleteObjCMethodsFromDWARF).
  CompilerType func_type = builder.CreateFunctionType(
      return_type, /*is_variadic=*/false,
      /*use_void_for_empty_params=*/sig.GetNumTypes() == 3);
  for (size_t i = 3; i < sig.GetNumTypes(); ++i) {
    llvm::StringRef param_enc = sig.GetTypeAtIndex(i);
    CompilerType param_type = RealizeObjCEncoding(builder, param_enc);
    if (!param_type)
      return;
    builder.AddParameter(func_type, param_type);
  }

  std::string full_name = (llvm::Twine(is_class_method ? "+[" : "-[") +
                           class_name + " " + selector + "]")
                              .str();
  builder.AddObjCMethod(iface, full_name, func_type, /*asm_label=*/"",
                        is_class_method, /*is_variadic=*/false);
}

CompilerType
TypeSystemCpp::CreateRuntimeObjCInterface(ConstString class_name,
                                          Process &process,
                                          ObjCLanguageRuntime &runtime) {
  // One runtime-built type per class name in this scratch context.
  if (auto it = m_runtime_objc_types.find(class_name.GetStringRef());
      it != m_runtime_objc_types.end())
    return GetCompilerType(it->second);

  ObjCLanguageRuntime::ClassDescriptorSP descriptor =
      runtime.GetClassDescriptorFromClassName(class_name);
  if (!descriptor) {
    // Dynamically-registered classes are often missing from the runtime's
    // name->isa map. The `OBJC_CLASS_$_<name>` symbol's address is exactly the
    // isa an instance of the class carries, so look the descriptor up by that.
    ConstString class_symbol(
        ("OBJC_CLASS_$_" + class_name.GetStringRef()).str());
    SymbolContextList sc_list;
    process.GetTarget().GetImages().FindSymbolsWithNameAndType(
        class_symbol, lldb::eSymbolTypeObjCClass, sc_list);
    for (const SymbolContext &sc : sc_list) {
      if (!sc.symbol)
        continue;
      lldb::addr_t isa = sc.symbol->GetLoadAddress(&process.GetTarget());
      if (isa == LLDB_INVALID_ADDRESS)
        continue;
      descriptor = runtime.GetClassDescriptorFromISA(isa);
      if (descriptor)
        break;
    }
  }
  if (!descriptor)
    return CompilerType();

  cpp_typesystem::Builder builder(*this);
  CompilerType iface_ct =
      builder.CreateObjCInterfaceType(class_name.GetStringRef(), std::nullopt);
  auto *iface = llvm::cast<cpp_typesystem::ObjCInterfaceType>(
      GetCppType(iface_ct.GetOpaqueQualType()));
  // Publish before filling so a self-referential ivar can't recurse forever.
  m_runtime_objc_types[class_name.GetStringRef()] = iface;

  // Chase the runtime's own superclass chain (rather than relying on any
  // debug info) so a class whose ivars are recovered from the runtime still
  // reports its inheritance -- e.g. `NSObject` as a child -- matching the
  // DWARF-derived case. Recursing here is safe: the class-name map above
  // guards against infinite recursion for any cycle, and a real ObjC
  // hierarchy is never cyclic.
  if (ObjCLanguageRuntime::ClassDescriptorSP super_descriptor =
          descriptor->GetSuperclass()) {
    ConstString super_name = super_descriptor->GetClassName();
    if (super_name && super_name != class_name) {
      CompilerType super_ct =
          CreateRuntimeObjCInterface(super_name, process, runtime);
      if (super_ct) {
        auto *super_iface = GetCppType(super_ct.GetOpaqueQualType());
        builder.SetObjCSuperClass(*iface, super_iface);
      }
    }
  }

  descriptor->Describe(
      /*superclass_func=*/nullptr,
      /*instance_method_func=*/
      [&](const char *name, const char *types) -> bool {
        AddRuntimeObjCMethod(builder, *iface, class_name.GetStringRef(), name,
                            types, /*is_class_method=*/false);
        return false;
      },
      /*class_method_func=*/
      [&](const char *name, const char *types) -> bool {
        AddRuntimeObjCMethod(builder, *iface, class_name.GetStringRef(), name,
                            types, /*is_class_method=*/true);
        return false;
      },
      [&](const char *name, const char *type, lldb::addr_t offset_ptr,
          uint64_t size) -> bool {
        if (!name || !name[0])
          return false;
        // The runtime stores a pointer to the ivar's (32-bit) byte offset.
        Status error;
        uint64_t byte_offset =
            process.ReadUnsignedIntegerFromMemory(offset_ptr, 4, 0, error);
        llvm::StringRef enc(type ? type : "");
        CompilerType ivar_type = RealizeObjCEncoding(builder, enc);
        // Fall back to an opaque byte blob of the right size so a member we
        // can't decode still occupies its slot in the layout.
        if (!ivar_type)
          ivar_type = builder.CreateArrayType(
              builder.GetBuiltinType("char", 1,
                                     lldb::eEncodingSint, lldb::eFormatChar),
              size);
        auto *field_type =
            GetCppType(ivar_type.GetOpaqueQualType());
        builder.AddField(*iface, builder.GetIdentifier(name), field_type,
                         byte_offset);
        return false;
      });
  builder.SetRecordComplete(*iface);
  return iface_ct;
}

CompilerType
TypeSystemCpp::GetRuntimeCompletedObjCType(cpp_typesystem::Type *t,
                                           const ExecutionContext *exe_ctx) {
  auto *objc = llvm::dyn_cast_or_null<cpp_typesystem::ObjCInterfaceType>(t);
  if (!objc)
    return CompilerType();
  // A runtime-built scratch type is already authoritative; asking the runtime
  // again would just rebuild an identical copy.
  if (llvm::is_contained(llvm::make_second_range(m_runtime_objc_types), objc))
    return CompilerType();
  // Prefer the caller's execution context, but fall back to the most recent
  // process seen through any exe_ctx-carrying query (see m_last_seen_process_wp)
  // -- the SBFrame::GetValueForVariablePath name-lookup path calls
  // GetIndexOfChildMemberWithName with no exe_ctx at all, yet must still resolve
  // a hidden ivar reconstructed from the runtime.
  Process *process = exe_ctx ? exe_ctx->GetProcessPtr() : nullptr;
  lldb::ProcessSP process_sp;
  if (process)
    m_last_seen_process_wp = process->shared_from_this();
  else {
    process_sp = m_last_seen_process_wp.lock();
    process = process_sp.get();
  }
  if (!process)
    return CompilerType();
  ObjCLanguageRuntime *runtime = ObjCLanguageRuntime::Get(*process);
  if (!runtime)
    return CompilerType();
  Target &target = process->GetTarget();
  auto scratch_or =
      target.GetScratchTypeSystemForLanguage(lldb::eLanguageTypeObjC_plus_plus);
  if (!scratch_or) {
    llvm::consumeError(scratch_or.takeError());
    return CompilerType();
  }
  auto *scratch = llvm::dyn_cast_or_null<TypeSystemCpp>(scratch_or->get());
  // Never redirect within the scratch context itself (that would recurse), and
  // require a distinct scratch context to hold the runtime data.
  if (!scratch || scratch == this)
    return CompilerType();
  ConstString class_name(objc->GetName().GetName());
  if (!class_name)
    return CompilerType();
  CompilerType runtime_ct =
      scratch->CreateRuntimeObjCInterface(class_name, *process, *runtime);
  if (!runtime_ct)
    return CompilerType();
  // Some (or all) of a class's ivars may live outside the debug info this
  // module type was completed from -- e.g. ivars added in a class extension
  // defined in a different image/CU than the one that produced this stub, so
  // no same-module or same-debug-map DWARF search finds them (see
  // DWARFASTParserCpp::ParseStructureType). Only prefer the runtime's answer
  // when it actually knows about more ivars than debug info did; otherwise
  // keep answering from the (potentially richer, e.g. better-typed) debug-info
  // fields directly.
  auto *runtime_iface = llvm::cast<cpp_typesystem::ObjCInterfaceType>(
      GetCppType(runtime_ct.GetOpaqueQualType()));
  if (runtime_iface->GetNumFields() <= objc->GetNumFields())
    return CompilerType();
  return runtime_ct;
}

llvm::Expected<uint32_t>
TypeSystemCpp::GetNumChildren(opaque_compiler_type_t type,
                             bool omit_empty_base_classes,
                              const ExecutionContext *exe_ctx) {
  if (!type)
    return 0;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // An array's children are its elements.
  if (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(t)) {
    if (std::optional<uint64_t> n = array->GetNumElements())
      return *n;
    // No static bound: this may be a variable-length array whose length is only
    // known at runtime. Ask the symbol file to resolve it for this frame.
    if (exe_ctx && array->GetDIEUID() != LLDB_INVALID_UID)
      if (SymbolFile *sym_file = GetSymbolFile())
        if (std::optional<SymbolFile::ArrayInfo> info =
                sym_file->GetDynamicArrayInfoForUID(array->GetDIEUID(), exe_ctx))
          if (!info->element_orders.empty())
            return info->element_orders.back().value_or(0);
    return 0;
  }
  // A pointer is transparent, mirroring TypeSystemClang: expanding `ptr`
  // splices in the pointee aggregate's members rather than showing a single
  // deref child. This is what the shared libc++ synthetic-child providers
  // expect (they call GetChildMemberWithName/GetChildAtIndex directly on
  // pointer-typed node values). As with references, only an already-complete
  // aggregate is expanded transparently so that merely counting a pointer's
  // children doesn't force completion of an otherwise-lazy pointee; a pointer
  // to an incomplete/non-aggregate pointee keeps its single deref child.
  // `void *` has no children.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (!pointee)
      return 0;
    // An Objective-C object is only ever accessed through a pointer (there is
    // no by-value ObjC object), so a pointer to an ObjC interface is always
    // transparent -- its children are the interface's ivars/superclass.
    if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(Desugar(pointee)))
      return GetNumChildren(static_cast<opaque_compiler_type_t>(pointee),
                            omit_empty_base_classes, exe_ctx);
    if (pointee->IsAggregate() && pointee->IsComplete())
      return GetNumChildren(static_cast<opaque_compiler_type_t>(pointee),
                            omit_empty_base_classes, exe_ctx);
    return 1;
  }
  // A reference is transparent: its children are those of the referenced type.
  // Like pointers, counting them must not force completion of the referent.
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (!pointee)
      return 0;
    if (pointee->IsAggregate() && pointee->IsComplete())
      return GetNumChildren(pointee, omit_empty_base_classes, exe_ctx);
    return 1;
  }
  if (!t->IsAggregate())
    return 0;
  // An ObjC interface with no debug-info ivars is completed from the runtime
  // (into the scratch context); answer from that completed type.
  if (CompilerType rt = GetRuntimeCompletedObjCType(t, exe_ctx))
    return rt.GetNumChildren(omit_empty_base_classes, exe_ctx);
  GetCompleteType(type);
  // A record/enum with no debug info defining it anywhere (e.g.
  // -flimit-debug-info hid its only definition, or a forward-declared
  // `struct Opaque;` that is never defined at all) stays incomplete even
  // after GetCompleteType, since TypeSystemCpp deliberately does not
  // force-complete it as an empty definition the way TypeSystemClang does
  // (see GetCompleteType's comment). TypeSystemClang's completion source
  // always leaves a Record reporting *some* field count (0, if it could only
  // force-complete an empty definition) rather than erroring at this level --
  // GetCompleteRecordType never returns null for a Record, so its GetNumChildren
  // Record case practically never takes its "incomplete" branch. Match that by
  // reporting zero children (not an error) here; a pointer/reference to such a
  // type still surfaces the error through GetChildCompilerTypeAtIndex's pointer
  // branch instead (see TestValueObjectErrors), which is the scenario the
  // error string was actually meant for.
  if (!t->IsComplete())
    return 0;
  // Children of a record are its direct base classes followed by its fields.
  // Empty base classes (no data members, recursively) are omitted when
  // requested, matching TypeSystemClang.
  uint32_t num_bases = t->GetNumBaseClasses();
  if (omit_empty_base_classes) {
    auto complete = [this](cpp_typesystem::Type *bt) {
      GetCompleteType(static_cast<opaque_compiler_type_t>(bt));
    };
    uint32_t non_empty_bases = 0;
    for (uint32_t i = 0; i < num_bases; ++i) {
      const cpp_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
      if (base && RecordHasFields(base->type.Get(), complete))
        ++non_empty_bases;
    }
    num_bases = non_empty_bases;
  }
  return num_bases + t->GetNumFields();
}

BasicType TypeSystemCpp::GetBasicTypeEnumeration(opaque_compiler_type_t type) {
  using cpp_typesystem::BuiltinKind;
  if (!type)
    return eBasicTypeInvalid;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  auto *builtin = llvm::dyn_cast<cpp_typesystem::BuiltinType>(t);
  if (!builtin)
    return eBasicTypeInvalid;

  if (std::optional<BuiltinKind> kind = builtin->GetBuiltinKind()) {
    switch (*kind) {
    case BuiltinKind::Void:            return eBasicTypeVoid;
    case BuiltinKind::Bool:            return eBasicTypeBool;
    case BuiltinKind::Char:            return eBasicTypeChar;
    case BuiltinKind::SignedChar:      return eBasicTypeSignedChar;
    case BuiltinKind::UnsignedChar:    return eBasicTypeUnsignedChar;
    case BuiltinKind::WCharT:          return eBasicTypeWChar;
    case BuiltinKind::Char8:           return eBasicTypeChar8;
    case BuiltinKind::Char16:          return eBasicTypeChar16;
    case BuiltinKind::Char32:          return eBasicTypeChar32;
    case BuiltinKind::Short:           return eBasicTypeShort;
    case BuiltinKind::UnsignedShort:   return eBasicTypeUnsignedShort;
    case BuiltinKind::Int:             return eBasicTypeInt;
    case BuiltinKind::UnsignedInt:     return eBasicTypeUnsignedInt;
    case BuiltinKind::Long:            return eBasicTypeLong;
    case BuiltinKind::UnsignedLong:    return eBasicTypeUnsignedLong;
    case BuiltinKind::LongLong:        return eBasicTypeLongLong;
    case BuiltinKind::UnsignedLongLong:return eBasicTypeUnsignedLongLong;
    case BuiltinKind::Int128:          return eBasicTypeInt128;
    case BuiltinKind::UnsignedInt128:  return eBasicTypeUnsignedInt128;
    case BuiltinKind::Float:           return eBasicTypeFloat;
    case BuiltinKind::Double:          return eBasicTypeDouble;
    case BuiltinKind::LongDouble:      return eBasicTypeLongDouble;
    case BuiltinKind::NullPtr:         return eBasicTypeNullPtr;
    case BuiltinKind::NumKinds:        break;
    }
  }
  return eBasicTypeInvalid;
}

bool TypeSystemCpp::IsPromotableIntegerType(opaque_compiler_type_t type) {
  // Follows C++ [conv.prom]: integer types with a conversion rank less than
  // int, plus bool/character/unscoped-enum types, promote to int/unsigned int.
  switch (GetBasicTypeEnumeration(type)) {
  case eBasicTypeBool:
  case eBasicTypeChar:
  case eBasicTypeSignedChar:
  case eBasicTypeUnsignedChar:
  case eBasicTypeShort:
  case eBasicTypeUnsignedShort:
  case eBasicTypeWChar:
  case eBasicTypeSignedWChar:
  case eBasicTypeUnsignedWChar:
  case eBasicTypeChar8:
  case eBasicTypeChar16:
  case eBasicTypeChar32:
    return true;
  default:
    break;
  }
  // Unscoped enumerations also promote.
  bool enum_is_signed = false;
  return IsEnumerationType(type, enum_is_signed) &&
         !IsScopedEnumerationType(type);
}

CompilerType
TypeSystemCpp::GetPromotedIntegerType(opaque_compiler_type_t type) {
  if (!IsPromotableIntegerType(type))
    return CompilerType();

  CompilerType int_type = GetBasicTypeFromAST(eBasicTypeInt);
  std::optional<uint64_t> int_size =
      GetCppType(int_type.GetOpaqueQualType())->GetByteSize();

  // Unscoped enumerations without a fixed underlying type promote to the first
  // of {int, unsigned int, long, ...} that can represent all enumerator values.
  // For the common case where the values fit in int, that is int -- regardless
  // of whether the DWARF underlying integer happens to be unsigned. Match
  // Clang, which computes the promotion type from the value range.
  if (auto *enum_type = llvm::dyn_cast<cpp_typesystem::EnumType>(
          Desugar(GetCppType(type)))) {
    bool needs_unsigned = false;
    if (int_size) {
      const uint64_t int_bits = *int_size * 8;
      // int can represent [-2^(n-1), 2^(n-1) - 1].
      const int64_t int_min = -(int64_t(1) << (int_bits - 1));
      const uint64_t int_max = (uint64_t(1) << (int_bits - 1)) - 1;
      const bool enum_signed = enum_type->IsSigned();
      for (const cpp_typesystem::Enumerator &e : enum_type->GetEnumerators()) {
        if (enum_signed) {
          int64_t v = static_cast<int64_t>(e.value);
          if (v < int_min || (v >= 0 && static_cast<uint64_t>(v) > int_max)) {
            needs_unsigned = true;
            break;
          }
        } else if (e.value > int_max) {
          needs_unsigned = true;
          break;
        }
      }
    }
    if (needs_unsigned)
      return GetBasicTypeFromAST(eBasicTypeUnsignedInt);
    return int_type;
  }

  // The result of integer promotion is `int` if int can represent all values
  // of the source type, otherwise `unsigned int`. Compare byte sizes and
  // signedness against int.
  std::optional<uint64_t> src_size = GetCppType(type)->GetByteSize();

  bool is_signed = false;
  bool src_is_integer = IsIntegerType(type, is_signed);

  if (src_size && int_size && *src_size >= *int_size && src_is_integer &&
      !is_signed) {
    // An unsigned source that is at least as wide as int cannot be represented
    // by int; promote to unsigned int instead.
    return GetBasicTypeFromAST(eBasicTypeUnsignedInt);
  }
  return int_type;
}

uint32_t TypeSystemCpp::GetNumFields(opaque_compiler_type_t type) {
  if (!type)
    return 0;
  GetCompleteType(type);
  // A pointer to an ObjC interface answers field queries as the interface
  // itself would (an ObjC object is only ever accessed through a pointer).
  return GetObjCBaseClassBearingType(type)->GetNumFields();
}

CompilerType TypeSystemCpp::GetFieldAtIndex(opaque_compiler_type_t type,
                                            size_t idx, std::string &name,
                                            uint64_t *bit_offset_ptr,
                                            uint32_t *bitfield_bit_size_ptr,
                                            bool *is_bitfield_ptr) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  const Field *field = GetObjCBaseClassBearingType(type)->GetFieldAtIndex(idx);
  if (!field)
    return CompilerType();
  name = field->name.GetName().str();
  if (bit_offset_ptr)
    *bit_offset_ptr = field->byte_offset * 8 + field->bitfield_bit_offset;
  if (bitfield_bit_size_ptr)
    *bitfield_bit_size_ptr = field->bitfield_bit_size;
  if (is_bitfield_ptr)
    *is_bitfield_ptr = field->IsBitfield();
  return GetCompilerType(field->type.Get());
}

CompilerDecl TypeSystemCpp::GetStaticFieldWithName(opaque_compiler_type_t type,
                                                   llvm::StringRef name) {
  if (!type)
    return CompilerDecl();
  GetCompleteType(type);
  auto *record =
      llvm::dyn_cast<cpp_typesystem::RecordType>(Desugar(GetCppType(type)));
  if (!record)
    return CompilerDecl();
  for (uint32_t i = 0, n = record->GetNumStaticDataMembers(); i < n; ++i) {
    const cpp_typesystem::StaticDataMember *member =
        record->GetStaticDataMemberAtIndex(i);
    if (member->name.GetName() == name)
      // The opaque decl is a tagged cpp_typesystem::Decl; the Decl* query
      // methods (DeclGetName / GetTypeForDecl / DeclGetConstantValue) interpret
      // it.
      return CompilerDecl(
          this, const_cast<cpp_typesystem::Decl *>(m_context.GetOrCreateDecl(
                    cpp_typesystem::Decl::Kind::StaticDataMember, member)));
  }
  return CompilerDecl();
}

cpp_typesystem::Type *
TypeSystemCpp::GetObjCBaseClassBearingType(opaque_compiler_type_t type) {
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // An Objective-C object is always handled through a pointer (`Foo *`), so a
  // pointer to an ObjC interface answers base-class queries as the interface
  // `Foo` itself would. This does not apply to ordinary C++ pointers.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t))
    if (cpp_typesystem::Type *pointee = ptr->GetPointeeType())
      if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(Desugar(pointee))) {
        GetCompleteType(static_cast<opaque_compiler_type_t>(pointee));
        return Desugar(pointee);
      }
  return t;
}

uint32_t TypeSystemCpp::GetNumDirectBaseClasses(opaque_compiler_type_t type) {
  if (!type)
    return 0;
  GetCompleteType(type);
  return GetObjCBaseClassBearingType(type)->GetNumBaseClasses();
}

uint32_t TypeSystemCpp::GetNumVirtualBaseClasses(opaque_compiler_type_t type) {
  return 0;
}

bool TypeSystemCpp::IsAnonymousType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // An anonymous struct/union (an unnamed record embedded as an unnamed member
  // of its parent) is marked as such by the DWARF parser. Look through sugar,
  // matching TypeSystemClang's RemoveWrappingTypes.
  if (auto *record =
          llvm::dyn_cast<cpp_typesystem::RecordType>(Desugar(GetCppType(type))))
    return record->IsAnonymousStructOrUnion();
  return false;
}

CompilerType
TypeSystemCpp::GetDirectBaseClassAtIndex(opaque_compiler_type_t type,
                                         size_t idx, uint32_t *bit_offset_ptr) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  const cpp_typesystem::BaseClass *base =
      GetObjCBaseClassBearingType(type)->GetBaseClassAtIndex(idx);
  if (!base)
    return CompilerType();
  if (bit_offset_ptr)
    *bit_offset_ptr = base->byte_offset * 8;
  return GetCompilerType(base->type.Get());
}

CompilerType TypeSystemCpp::GetVirtualBaseClassAtIndex(
    opaque_compiler_type_t type, size_t idx, uint32_t *bit_offset_ptr) {
  return CompilerType();
}

llvm::Expected<CompilerType> TypeSystemCpp::GetDereferencedType(
    opaque_compiler_type_t type, ExecutionContext *exe_ctx,
    std::string &deref_name, uint32_t &deref_byte_size,
    int32_t &deref_byte_offset, ValueObject *valobj, uint64_t &language_flags) {
  // Only pointers, references and arrays can be dereferenced.
  if (!IsPointerOrReferenceType(type, nullptr) &&
      !IsArrayType(type, nullptr, nullptr, nullptr))
    return llvm::createStringError("not a pointer, reference or array type");

  // A `void *` cannot be dereferenced. Report a specific error (matching
  // TypeSystemClang) rather than falling through to a zero-sized child, which
  // would surface a generic "dereference failed" message.
  if (IsPointerType(type, nullptr) && GetPointeeType(type).IsVoidType())
    return llvm::createStringError("cannot dereference void *");

  // The dereferenced value is child 0. Ask for it non-transparently so a
  // pointer-to-aggregate yields the pointee itself rather than its members.
  uint32_t child_bitfield_bit_size = 0;
  uint32_t child_bitfield_bit_offset = 0;
  bool child_is_base_class = false;
  bool child_is_deref_of_parent = false;
  return GetChildCompilerTypeAtIndex(
      type, exe_ctx, /*idx=*/0, /*transparent_pointers=*/false,
      /*omit_empty_base_classes=*/true, /*ignore_array_bounds=*/false,
      deref_name, deref_byte_size, deref_byte_offset, child_bitfield_bit_size,
      child_bitfield_bit_offset, child_is_base_class, child_is_deref_of_parent,
      valobj, language_flags);
}

llvm::Expected<CompilerType> TypeSystemCpp::GetChildCompilerTypeAtIndex(
    opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {
  child_name.clear();
  child_byte_size = 0;
  child_byte_offset = 0;
  child_bitfield_bit_size = 0;
  child_bitfield_bit_offset = 0;
  child_is_base_class = false;
  child_is_deref_of_parent = false;
  language_flags = 0;

  if (!type)
    return CompilerType();
  GetCompleteType(type);
  cpp_typesystem::Type *t = Desugar(GetCppType(type));

  // Array elements: child N is the element at offset N * element_size.
  if (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(t)) {
    if (!ignore_array_bounds) {
      std::optional<uint64_t> num_elements = array->GetNumElements();
      if (num_elements && idx >= *num_elements)
        return CompilerType();
    }
    cpp_typesystem::Type *element_type = array->GetElementType();
    child_name = llvm::formatv("[{0}]", idx).str();
    if (std::optional<uint64_t> byte_size = element_type->GetByteSize()) {
      child_byte_size = *byte_size;
      child_byte_offset = idx * *byte_size;
    }
    return GetCompilerType(element_type);
  }

  // A pointer is transparent, mirroring TypeSystemClang (and the reference
  // case below): when asked transparently, expanding it splices in the pointee
  // aggregate's members instead of yielding a single deref child. Only an
  // already-complete aggregate is expanded transparently so that merely
  // inspecting a pointer doesn't force completion of an otherwise-lazy pointee;
  // otherwise child 0 is the dereferenced value (which the DIL `ptr->member`
  // path relies on).
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (!pointee)
      return CompilerType(); // Can't dereference `void *`.

    // A pointer to an ObjC interface, like any other aggregate pointee, is
    // only expanded transparently when explicitly asked (transparent_pointers)
    // -- e.g. `--ptr-depth`/child enumeration -- matching TypeSystemClang's
    // ObjCObjectPointer case. A non-transparent access (idx 0, as used by
    // `*ptr`/GetDereferencedType) must fall through to the "child 0 is the
    // dereferenced value" path below so `*ptr` yields the whole pointee
    // object rather than its first base/ivar.
    bool is_objc =
        llvm::isa<cpp_typesystem::ObjCInterfaceType>(Desugar(pointee));
    if (is_objc)
      GetCompleteType(GetCompilerType(pointee).GetOpaqueQualType());
    if (transparent_pointers &&
        (is_objc || (pointee->IsAggregate() && pointee->IsComplete()))) {
      bool tmp_child_is_deref_of_parent = false;
      return GetCompilerType(pointee).GetChildCompilerTypeAtIndex(
          exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset,
          child_is_base_class, tmp_child_is_deref_of_parent, valobj,
          language_flags);
    }

    child_is_deref_of_parent = true;
    if (const char *parent_name =
            valobj ? valobj->GetName().GetCString() : nullptr) {
      child_name.assign(1, '*');
      child_name += parent_name;
    }
    if (idx != 0)
      return CompilerType();
    // Dereferencing yields the pointee by value, so it must be complete now
    // (this is the explicit access that is allowed to force completion of an
    // otherwise-lazy pointee).
    GetCompleteType(GetCompilerType(pointee).GetOpaqueQualType());
    std::optional<uint64_t> byte_size = pointee->GetByteSize();
    if (!byte_size)
      return llvm::createStringError(
          "incomplete type \"" +
          GetDisplayTypeName(GetCompilerType(pointee).GetOpaqueQualType())
              .GetString() +
          "\"");
    child_byte_size = *byte_size;
    child_byte_offset = 0;
    return GetCompilerType(pointee);
  }

  // A reference is transparent, just like a pointer: expanding it either shows
  // the referenced aggregate's members or the single referenced value.
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (!pointee)
      return CompilerType();

    // As with pointers, only expand an already-complete referent transparently
    // so that merely inspecting the reference doesn't force its completion.
    if (transparent_pointers && pointee->IsAggregate() &&
        pointee->IsComplete()) {
      bool tmp_child_is_deref_of_parent = false;
      return GetCompilerType(pointee).GetChildCompilerTypeAtIndex(
          exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset,
          child_is_base_class, tmp_child_is_deref_of_parent, valobj,
          language_flags);
    }

    child_is_deref_of_parent = true;
    if (const char *parent_name =
            valobj ? valobj->GetName().GetCString() : nullptr) {
      child_name.assign(1, '&');
      child_name += parent_name;
    }
    if (idx != 0)
      return CompilerType();
    // As for pointers, materializing the referent forces its completion.
    GetCompleteType(GetCompilerType(pointee).GetOpaqueQualType());
    if (std::optional<uint64_t> byte_size = pointee->GetByteSize())
      child_byte_size = *byte_size;
    child_byte_offset = 0;
    return GetCompilerType(pointee);
  }

  // An ObjC interface with no debug-info ivars is completed from the runtime
  // (into the scratch context); resolve children against that completed type.
  if (CompilerType rt = GetRuntimeCompletedObjCType(t, exe_ctx))
    return rt.GetChildCompilerTypeAtIndex(
        exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
        ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
        child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
        child_is_deref_of_parent, valobj, language_flags);

  // Children are laid out as the direct base classes followed by the fields.
  // When omit_empty_base_classes is set, empty base classes (no data members,
  // recursively) are skipped and do not consume a child index, matching
  // TypeSystemClang.
  auto complete = [this](cpp_typesystem::Type *bt) {
    GetCompleteType(static_cast<opaque_compiler_type_t>(bt));
  };
  uint32_t total_bases = t->GetNumBaseClasses();
  uint32_t visible_base_idx = 0;
  for (uint32_t i = 0; i < total_bases; ++i) {
    const cpp_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    if (!base)
      continue;
    if (omit_empty_base_classes &&
        !RecordHasFields(base->type.Get(), complete))
      continue;
    if (visible_base_idx == idx) {
      // Name the base-class child by its (possibly sugar-wrapped) type name
      // rather than the raw record name: a base recovered for an expression
      // result can be an elaborated/spelling-sugar wrapper whose own m_name is
      // empty (the real name sits on the underlying type), so fall back to the
      // display type name in that case.
      child_name = base->type.Get()->GetName().GetName().str();
      if (child_name.empty())
        child_name =
            GetCompilerType(base->type.Get()).GetTypeName().GetString();
      child_byte_offset = base->byte_offset;
      // A virtual base has no constant offset: its subobject can sit at
      // different places in different most-derived objects (the diamond case).
      // Read the real offset from the live object's vtable when we can, so e.g.
      // Joiner1.Derived1.VBase and Joiner1.Derived2.VBase resolve to the one
      // shared subobject. Falls back to byte_offset (0) if no live object.
      if (base->is_virtual) {
        if (std::optional<int64_t> vbase_off =
                ReadVirtualBaseOffset(*base, valobj))
          child_byte_offset = *vbase_off;
      }
      if (std::optional<uint64_t> byte_size = base->type.Get()->GetByteSize())
        child_byte_size = *byte_size;
      child_is_base_class = true;
      return GetCompilerType(base->type.Get());
    }
    ++visible_base_idx;
  }

  const Field *field = t->GetFieldAtIndex(idx - visible_base_idx);
  if (!field)
    return CompilerType();

  child_name = field->name.GetName().str();
  child_byte_offset = field->byte_offset;
  // An Objective-C ivar's byte offset is not reliably encoded in DWARF (the
  // compiler emits 0 for every ivar); the authoritative offset lives in the
  // ObjC runtime's `OBJC_IVAR_$_Class.ivar` symbols. Resolve it against the
  // live process when possible, falling back to the DWARF offset otherwise.
  if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(t) && exe_ctx) {
    if (Process *process = exe_ctx->GetProcessPtr()) {
      if (ObjCLanguageRuntime *objc_runtime =
              ObjCLanguageRuntime::Get(*process)) {
        CompilerType parent_type = GetCompilerType(t);
        size_t ivar_offset = objc_runtime->GetByteOffsetForIvar(
            parent_type, field->name.GetName().str().c_str());
        if (ivar_offset != static_cast<size_t>(LLDB_INVALID_IVAR_OFFSET))
          child_byte_offset = ivar_offset;
      }
    }
  }
  if (std::optional<uint64_t> byte_size = field->type.Get()->GetByteSize())
    child_byte_size = *byte_size;
  if (field->IsBitfield()) {
    child_bitfield_bit_size = field->bitfield_bit_size;
    child_bitfield_bit_offset = field->bitfield_bit_offset;
  }
  return GetCompilerType(field->type.Get());
}

llvm::Expected<uint32_t>
TypeSystemCpp::GetIndexOfChildWithName(opaque_compiler_type_t type,
                                       llvm::StringRef name,
                                       bool omit_empty_base_classes) {
  if (!type)
    return llvm::createStringError("invalid type");
  GetCompleteType(type);
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // See GetIndexOfChildMemberWithNameImpl: redirect an ObjC interface whose
  // ivars come from the runtime (e.g. a hidden ivar in a stripped image) to the
  // runtime-completed scratch type so a by-name lookup finds it.
  if (CompilerType rt = GetRuntimeCompletedObjCType(t, /*exe_ctx=*/nullptr))
    return rt.GetTypeSystem()->GetIndexOfChildWithName(
        rt.GetOpaqueQualType(), name, omit_empty_base_classes);
  // TypeSystemClang): its named children are the aggregate pointee's members,
  // so forward the lookup. A pointer to a non-aggregate has only its (unnamed)
  // deref child, so no named member is directly addressable.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (pointee && pointee->IsAggregate())
      return GetCompilerType(pointee).GetIndexOfChildWithName(
          name, omit_empty_base_classes);
    return llvm::createStringError(
        "TypeSystemCpp::GetIndexOfChildWithName: no such child");
  }
  cpp_typesystem::Type *pointee = nullptr;
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t))
    pointee = ref->GetPointeeType();
  if (pointee) {
    if (pointee->IsAggregate())
      return GetCompilerType(pointee).GetIndexOfChildWithName(
          name, omit_empty_base_classes);
    return llvm::createStringError(
        "TypeSystemCpp::GetIndexOfChildWithName: no such child");
  }
  // Base classes are the first children (empty ones omitted when requested,
  // matching GetChildCompilerTypeAtIndex); match them by their type name.
  auto complete = [this](cpp_typesystem::Type *bt) {
    GetCompleteType(static_cast<opaque_compiler_type_t>(bt));
  };
  uint32_t total_bases = t->GetNumBaseClasses();
  uint32_t visible_base_idx = 0;
  for (uint32_t i = 0; i < total_bases; ++i) {
    const cpp_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    if (!base)
      continue;
    if (omit_empty_base_classes &&
        !RecordHasFields(base->type.Get(), complete))
      continue;
    if (base->type.Get()->GetName().GetName() == name)
      return visible_base_idx;
    // A sugar-wrapped base (see GetChildCompilerTypeAtIndex) has an empty raw
    // name; match it by its display type name instead.
    if (base->type.Get()->GetName().GetName().empty() &&
        GetCompilerType(base->type.Get()).GetTypeName().GetStringRef() == name)
      return visible_base_idx;
    ++visible_base_idx;
  }
  // Fields follow the base classes.
  for (uint32_t i = 0, e = t->GetNumFields(); i < e; ++i) {
    if (t->GetFieldAtIndex(i)->name.GetName() == name)
      return visible_base_idx + i;
  }
  return llvm::createStringError(
      "TypeSystemCpp::GetIndexOfChildWithName: no such child");
}

size_t TypeSystemCpp::GetIndexOfChildMemberWithName(
    opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  // The record the lookup starts from is allowed to transparently search its
  // anonymous (unnamed union/struct) fields; recursion into base classes is not
  // (see GetIndexOfChildMemberWithNameImpl).
  return GetIndexOfChildMemberWithNameImpl(type, name, omit_empty_base_classes,
                                           /*descend_anon_fields=*/true,
                                           child_indexes);
}

size_t TypeSystemCpp::GetIndexOfChildMemberWithNameImpl(
    opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, bool descend_anon_fields,
    std::vector<uint32_t> &child_indexes) {
  if (!type)
    return 0;
  GetCompleteType(type);
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // An ObjC interface whose ivars are not in the debug info (e.g. a hidden ivar
  // declared in a class extension in a stripped image) is completed from the
  // runtime into the scratch context; forward the member lookup to that
  // completed type. This mirrors the child-enumeration paths (GetNumChildren /
  // GetChildCompilerTypeAtIndex), which also redirect -- keeping name->index
  // consistent with the child layout. No exe_ctx is available here (the
  // SBFrame::GetValueForVariablePath path doesn't provide one), so
  // GetRuntimeCompletedObjCType falls back to the last-seen process.
  if (CompilerType rt = GetRuntimeCompletedObjCType(t, /*exe_ctx=*/nullptr))
    return rt.GetTypeSystem()->GetIndexOfChildMemberWithName(
        rt.GetOpaqueQualType(), name, omit_empty_base_classes, child_indexes);
  // so that e.g. `ptr->member` / `ref.member` resolves against the pointed-to
  // record. A pointer is now transparent (see GetNumChildren): its children are
  // the pointee aggregate's members directly, with no intervening deref child,
  // so recurse without pushing an index-0 deref step -- matching the reference
  // case and keeping the returned indices consistent with the child layout.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (!pointee || !pointee->IsAggregate())
      return 0;
    return GetIndexOfChildMemberWithNameImpl(
        static_cast<opaque_compiler_type_t>(pointee), name,
        omit_empty_base_classes, /*descend_anon_fields=*/true, child_indexes);
  }
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (pointee && pointee->IsAggregate())
      return GetIndexOfChildMemberWithNameImpl(
          static_cast<opaque_compiler_type_t>(pointee), name,
          omit_empty_base_classes, /*descend_anon_fields=*/true, child_indexes);
    return 0;
  }
  // Compute the number of visible base classes (empty ones omitted when
  // requested), since fields are laid out after them and their child indices
  // must match GetChildCompilerTypeAtIndex.
  auto complete = [this](cpp_typesystem::Type *bt) {
    GetCompleteType(static_cast<opaque_compiler_type_t>(bt));
  };
  uint32_t total_bases = t->GetNumBaseClasses();
  auto base_is_visible = [&](const cpp_typesystem::BaseClass *base) {
    return base && (!omit_empty_base_classes ||
                    RecordHasFields(base->type.Get(), complete));
  };
  uint32_t num_visible_bases = 0;
  for (uint32_t i = 0; i < total_bases; ++i)
    if (base_is_visible(t->GetBaseClassAtIndex(i)))
      ++num_visible_bases;

  // A matching field is a direct child, laid out after the base classes. An
  // unnamed field is an anonymous union/struct whose members are reached as if
  // they belonged to this record, so recurse into it -- but only when
  // `descend_anon_fields` is set (i.e. this is the record the lookup started
  // from, not a base class we recursed into).
  for (uint32_t i = 0, e = t->GetNumFields(); i < e; ++i) {
    const Field *field = t->GetFieldAtIndex(i);
    llvm::StringRef field_name = field->name.GetName();
    if (field_name == name) {
      child_indexes.push_back(num_visible_bases + i);
      return child_indexes.size();
    }
    if (descend_anon_fields && field_name.empty() && field->type) {
      std::vector<uint32_t> save_indices = child_indexes;
      child_indexes.push_back(num_visible_bases + i);
      // An anonymous field of the starting record still injects the members of
      // *its* anonymous fields, so keep descending transparently through it.
      if (GetIndexOfChildMemberWithNameImpl(
              static_cast<opaque_compiler_type_t>(field->type.Get()), name,
              omit_empty_base_classes, /*descend_anon_fields=*/true,
              child_indexes))
        return child_indexes.size();
      child_indexes = std::move(save_indices);
    }
  }

  // Otherwise the member may be inherited from a base class. Base classes are
  // the first children, so their child index is their visible position. When
  // recursing into a base we must NOT descend into that base's anonymous fields:
  // C++ name lookup does not find a member that is injected by an anonymous
  // field of a base class (only direct members and further base classes are
  // reachable), matching TypeSystemClang.
  uint32_t visible_base_idx = 0;
  for (uint32_t i = 0; i < total_bases; ++i) {
    const cpp_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    if (!base_is_visible(base))
      continue;
    std::vector<uint32_t> save_indices = child_indexes;
    child_indexes.push_back(visible_base_idx);
    if (GetIndexOfChildMemberWithNameImpl(
            static_cast<opaque_compiler_type_t>(base->type.Get()), name,
            omit_empty_base_classes, /*descend_anon_fields=*/false,
            child_indexes))
      return child_indexes.size();
    child_indexes = std::move(save_indices);
    ++visible_base_idx;
  }
  return 0;
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void TypeSystemCpp::dump(opaque_compiler_type_t type) const {}
#endif

/// Render an enum value as an enumerator name when it matches one exactly. For
/// a "bitfield/flag" enum (every enumerator is a single bit or a union of
/// previously-seen bits) a combined value is decomposed into `A | B`. Mirrors
/// TypeSystemClang's DumpEnumValue.
static bool DumpEnumValue(const cpp_typesystem::EnumType &enum_type, Stream &s,
                          const DataExtractor &data, lldb::offset_t byte_offset,
                          size_t byte_size, uint32_t bitfield_bit_offset,
                          uint32_t bitfield_bit_size) {
  lldb::offset_t offset = byte_offset;
  const bool is_signed = enum_type.IsSigned();
  const uint64_t enum_svalue =
      is_signed
          ? static_cast<uint64_t>(data.GetMaxS64Bitfield(
                &offset, byte_size, bitfield_bit_size, bitfield_bit_offset))
          : data.GetMaxU64Bitfield(&offset, byte_size, bitfield_bit_size,
                                   bitfield_bit_offset);

  bool can_be_bitfield = true;
  uint64_t covered_bits = 0;
  int num_enumerators = 0;

  // Look for an exact match while applying the bitfield heuristic: an enum is
  // likely a flag set if every enumerator is a single bit or a superset of the
  // bits seen so far.
  const std::vector<cpp_typesystem::Enumerator> &enumerators =
      enum_type.GetEnumerators();
  if (enumerators.empty())
    can_be_bitfield = false;
  for (const cpp_typesystem::Enumerator &enumerator : enumerators) {
    uint64_t val = enumerator.value;
    if (is_signed)
      val = llvm::SignExtend64(val, 8 * byte_size);
    if (llvm::popcount(val) != 1 && (val & ~covered_bits) != 0)
      can_be_bitfield = false;
    covered_bits |= val;
    ++num_enumerators;
    if (val == enum_svalue) {
      s.PutCString(enumerator.name.GetName());
      return true;
    }
  }

  // Unsigned values make more sense for flags.
  offset = byte_offset;
  const uint64_t enum_uvalue = data.GetMaxU64Bitfield(
      &offset, byte_size, bitfield_bit_size, bitfield_bit_offset);

  // No exact match and this isn't a flag enum: print the value numerically.
  if (!can_be_bitfield) {
    if (is_signed)
      s.Printf("%" PRIi64, static_cast<int64_t>(enum_svalue));
    else
      s.Printf("%" PRIu64, enum_uvalue);
    return true;
  }

  if (!enum_uvalue) {
    // Flag enum, but the value is 0 and matched no enumerator above.
    s.Printf("0x%" PRIx64, enum_uvalue);
    return true;
  }

  uint64_t remaining_value = enum_uvalue;
  std::vector<std::pair<uint64_t, llvm::StringRef>> values;
  values.reserve(num_enumerators);
  for (const cpp_typesystem::Enumerator &enumerator : enumerators)
    if (enumerator.value)
      values.emplace_back(enumerator.value, enumerator.name.GetName());

  // Sort by descending population count (stably) so that in
  // `enum { A, B, ALL = A|B }` we emit ALL before A/B, and `A | C` keeps the
  // declaration order for equal popcounts.
  llvm::stable_sort(values, [](const auto &a, const auto &b) {
    return llvm::popcount(a.first) > llvm::popcount(b.first);
  });

  for (const auto &val : values) {
    if ((remaining_value & val.first) != val.first)
      continue;
    remaining_value &= ~val.first;
    s.PutCString(val.second);
    if (remaining_value)
      s.PutCString(" | ");
  }

  // Print any bits not covered by an enumerator as hex.
  if (remaining_value)
    s.Printf("0x%" PRIx64, remaining_value);

  return true;
}

bool TypeSystemCpp::DumpTypeValue(opaque_compiler_type_t type, Stream &s,
                                  Format format, const DataExtractor &data,
                                  offset_t data_offset, size_t data_byte_size,
                                  uint32_t bitfield_bit_size,
                                  uint32_t bitfield_bit_offset,
                                  ExecutionContextScope *exe_scope) {
  if (!type)
    return false;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  // Aggregates don't have a scalar value to print; their children are dumped
  // individually.
  if (t->IsAggregate())
    return false;
  if (format == eFormatDefault)
    format = t->GetFormat();
  // Enumerations: show the enumerator name when possible rather than the raw
  // integer value.
  if (auto *enum_type = llvm::dyn_cast<cpp_typesystem::EnumType>(t)) {
    if (format == eFormatEnum || format == eFormatDefault)
      return DumpEnumValue(*enum_type, s, data, data_offset, data_byte_size,
                           bitfield_bit_offset, bitfield_bit_size);
  }
  // Some formats dump the value as a sequence of smaller items rather than one
  // scalar: e.g. the char/bytes formats print each byte, and the unicode
  // formats print each code unit. Split the byte size into that many items so
  // e.g. a 16-byte `__uint128_t` printed with a char format is shown as 16
  // characters instead of being rejected as too wide (matching TypeSystemClang).
  uint32_t item_count = 1;
  switch (format) {
  case eFormatChar:
  case eFormatCharPrintable:
  case eFormatCharArray:
  case eFormatBytes:
  case eFormatUnicode8:
  case eFormatBytesWithASCII:
    item_count = data_byte_size;
    data_byte_size = 1;
    break;
  case eFormatUnicode16:
    item_count = data_byte_size / 2;
    data_byte_size = 2;
    break;
  case eFormatUnicode32:
    item_count = data_byte_size / 4;
    data_byte_size = 4;
    break;
  default:
    break;
  }
  return DumpDataExtractor(data, &s, data_offset, format, data_byte_size,
                           item_count, UINT32_MAX, LLDB_INVALID_ADDRESS,
                           bitfield_bit_size, bitfield_bit_offset, exe_scope);
}

void TypeSystemCpp::DumpTypeDescription(opaque_compiler_type_t type,
                                        DescriptionLevel level) {
  StreamFile s(stdout, false);
  DumpTypeDescription(type, s, level);
}

// Append a C declarator ("<type> <name>") for a record member, using array
// declarator syntax (`char padding[0]`) when the member's type is an array so
// the printed definition matches C source form.
static void AppendMemberDecl(Stream &s, TypeSystemCpp &ts,
                             cpp_typesystem::Type *field_type,
                             llvm::StringRef name) {
  using namespace cpp_typesystem;
  std::string dims;
  cpp_typesystem::Type *cur = field_type;
  while (auto *array = llvm::dyn_cast_or_null<ArrayType>(cur)) {
    if (std::optional<uint64_t> n = array->GetNumElements())
      dims += llvm::formatv("[{0}]", *n).str();
    else
      dims += "[]";
    cur = array->GetElementType();
  }
  std::string base = ts.GetTypeName(cur, /*BaseOnly=*/false).GetStringRef().str();
  s << base;
  if (!name.empty())
    s << " " << name;
  s << dims;
}

// Append a clang-like member-function declaration ("<ret> <name>(<params>)
// <cv-qualifiers> <ref-qualifier>;") for a record's method, mirroring how
// clang::RecordDecl::print renders a CXXMethodDecl. Reuses BuildFunctionName's
// return-type/parameter rendering.
static void AppendMemberFunctionDecl(Stream &s,
                                     const cpp_typesystem::MemberFunction &m) {
  using namespace cpp_typesystem;
  if (m.is_static)
    s << "static ";
  FunctionType *fn = llvm::dyn_cast_or_null<FunctionType>(m.type.Get());
  std::string name = m.name.GetName().str();
  if (fn) {
    s << BuildFunctionName(fn, name);
  } else {
    s << name << "()";
  }
  if (m.is_const)
    s << " const";
  if (m.is_volatile)
    s << " volatile";
  switch (m.ref_qualifier) {
  case RefQualifier::None:
    break;
  case RefQualifier::LValue:
    s << " &";
    break;
  case RefQualifier::RValue:
    s << " &&";
    break;
  }
  s.PutCString(";\n");
}

void TypeSystemCpp::DumpTypeDescription(opaque_compiler_type_t type, Stream &s,
                                        DescriptionLevel level) {
  if (!type)
    return;
  // A typedef is checked before desugaring (unlike the record/enum/ObjC
  // branches below, which want the canonical type): clang's equivalent dump
  // only special-cases a type that IS itself a TypedefType, printing
  // "typedef <name>" and leaving the underlying type alone, matching
  // TypeSystemClang::DumpTypeDescription's `case clang::Type::Typedef`.
  if (llvm::isa<cpp_typesystem::TypedefType>(GetCppType(type))) {
    s.PutCString("typedef ");
    s.PutCString(GetTypeName(type, /*BaseOnly=*/true).GetStringRef());
    return;
  }
  cpp_typesystem::Type *t = Desugar(GetCppType(type));

  if (auto *enum_type = llvm::dyn_cast<cpp_typesystem::EnumType>(t)) {
    GetCompleteType(type);
    // Scoped enums (`enum class`) print their tag so the definition matches C++
    // source form. The class/struct distinction is not modeled, so scoped enums
    // always use `class` (clang's default spelling).
    const char *scope = enum_type->IsScoped() ? " class" : "";
    s.Printf("enum%s %s {\n", scope,
             GetTypeName(t, /*BaseOnly=*/false).GetCString());
    for (const cpp_typesystem::Enumerator &e : enum_type->GetEnumerators()) {
      if (enum_type->IsSigned())
        s.Printf("    %s = %" PRId64 ",\n", e.name.GetName().str().c_str(),
                 static_cast<int64_t>(e.value));
      else
        s.Printf("    %s = %" PRIu64 ",\n", e.name.GetName().str().c_str(),
                 e.value);
    }
    s.PutCString("}");
    return;
  }

  if (auto *iface = llvm::dyn_cast<cpp_typesystem::ObjCInterfaceType>(t)) {
    GetCompleteType(type);
    // An ObjC interface is a RecordType (ivars modeled as fields, superclass
    // as its one base class), but its source-level spelling is `@interface`,
    // not `struct`/`class` -- dump it separately from the generic RecordType
    // branch below (which it would otherwise also match).
    s.Printf("@interface %s", GetTypeName(t, /*BaseOnly=*/true).GetCString());
    if (const cpp_typesystem::BaseClass *super = iface->GetBaseClassAtIndex(0))
      s.Printf(" : %s",
               GetTypeName(super->type.Get(), /*BaseOnly=*/true).GetCString());
    s.PutCString(" {\n");
    for (uint32_t i = 0, e = iface->GetNumFields(); i != e; ++i) {
      const cpp_typesystem::Field *field = iface->GetFieldAtIndex(i);
      if (!field)
        continue;
      s.PutCString("    ");
      AppendMemberDecl(s, *this, field->type.Get(), field->name.GetName());
      s.PutCString(";\n");
    }
    s.PutCString("}\n");
    CompleteMemberFunctions(iface);
    for (uint32_t i = 0, e = iface->GetNumObjCMethods(); i != e; ++i) {
      if (const cpp_typesystem::ObjCMethod *m = iface->GetObjCMethodAtIndex(i))
        s.Printf("%s;\n", m->name.GetName().str().c_str());
    }
    return;
  }

  if (auto *record = llvm::dyn_cast<cpp_typesystem::RecordType>(t)) {
    GetCompleteType(type);
    // `struct` and `class` both map onto the same ClassType C++ class (the
    // keyword doesn't affect layout); the actual source-spelling keyword is
    // tracked separately via IsClassKeyword(). Consult that instead of the
    // C++ type hierarchy so `struct Foo` prints "struct Foo", not "class Foo".
    const char *tag = record->IsUnion()
                          ? "union"
                          : (record->IsClassKeyword() ? "class" : "struct");
    // This dump context matches clang's printer, which uses the bare
    // (unqualified) tag name here, not the fully-qualified name.
    s.Printf("%s %s {\n", tag,
             GetTypeName(t, /*BaseOnly=*/true).GetCString());
    for (uint32_t i = 0, e = record->GetNumFields(); i != e; ++i) {
      const cpp_typesystem::Field *field = record->GetFieldAtIndex(i);
      if (!field)
        continue;
      s.PutCString("    ");
      AppendMemberDecl(s, *this, field->type.Get(), field->name.GetName());
      if (field->IsBitfield())
        s.Printf(" : %u", field->bitfield_bit_size);
      s.PutCString(";\n");
    }
    CompleteMemberFunctions(record);
    for (uint32_t i = 0, e = record->GetNumMemberFunctions(); i != e; ++i) {
      const cpp_typesystem::MemberFunction *m =
          record->GetMemberFunctionAtIndex(i);
      if (!m)
        continue;
      s.PutCString("    ");
      AppendMemberFunctionDecl(s, *m);
    }
    s.PutCString("}");
    return;
  }

  // Anything else: just print its name.
  if (ConstString name = GetTypeName(t, /*BaseOnly=*/false))
    s.PutCString(name.GetStringRef());
}

void TypeSystemCpp::Dump(llvm::raw_ostream &output, llvm::StringRef filter,
                         bool show_color) {
  // Collect every record type this system has produced and hand them to the
  // Clang-AST synthesizer (which lives in the expression-parser plugin, since
  // TypeSystem/Cpp must not depend on the clang AST) to build and print a
  // throwaway clang AST. Backs `target modules dump ast`.
  std::vector<CompilerType> records;
  m_context.ForEachRecordType([&](cpp_typesystem::RecordType *record) {
    records.push_back(GetCompilerType(record));
  });
  ClangASTGenerator::DumpRecords(*this, m_triple, records, output, filter,
                                 show_color);
}

bool TypeSystemCpp::IsRuntimeGeneratedType(opaque_compiler_type_t type) {
  // An Objective-C class's layout (ivar offsets) is provided by the ObjC
  // runtime rather than being fixed by the debug info.
  return type && llvm::isa<cpp_typesystem::ObjCInterfaceType>(
                     Desugar(GetCppType(type)));
}

bool TypeSystemCpp::IsPointerOrReferenceType(opaque_compiler_type_t type,
                                             CompilerType *pointee_type) {
  if (IsPointerType(type, pointee_type))
    return true;
  return IsReferenceType(type, pointee_type, /*is_rvalue=*/nullptr);
}

unsigned TypeSystemCpp::GetTypeQualifiers(opaque_compiler_type_t type) {
  if (!type)
    return 0;
  // Return the CVR qualifier mask using clang's bit layout
  // (Const = 0x1, Restrict = 0x2, Volatile = 0x4), which is what the callers
  // (e.g. clang::Qualifiers::fromCVRMask) expect. DWARF nests one qualifier per
  // DIE, so `const volatile T` is modeled as two stacked CVQualifiedType nodes;
  // walk the whole cv-sugar chain and OR the flags so both are reported (as a
  // single clang `const volatile T` would).
  unsigned quals = 0;
  for (cpp_typesystem::Type *t = GetCppType(type); t;) {
    auto *cv = llvm::dyn_cast<cpp_typesystem::CVQualifiedType>(t);
    if (!cv)
      break;
    if (cv->IsConst())
      quals |= 0x1;
    if (cv->IsVolatile())
      quals |= 0x4;
    t = cv->GetUnderlyingType();
  }
  return quals;
}

std::optional<size_t>
TypeSystemCpp::GetTypeBitAlign(opaque_compiler_type_t type,
                              ExecutionContextScope *exe_scope) {
  if (!type)
    return std::nullopt;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  if (!t)
    return std::nullopt;

  // Pointers and references are pointer-aligned.
  if (llvm::isa<cpp_typesystem::PointerType>(t) ||
      llvm::isa<cpp_typesystem::ReferenceType>(t))
    return GetPointerByteSize() * 8;

  // Prefer an explicitly-recorded alignment (e.g. an `alignas(...)` type, whose
  // DW_AT_alignment the DWARF parser stored), which the size-derived heuristic
  // below cannot recover.
  if (std::optional<uint64_t> align = t->GetAlignInBits())
    if (*align != 0)
      return *align;

  // The cpp_typesystem model doesn't record alignment. For scalars this is the
  // size; for aggregates, derive an alignment that divides the size (natural
  // alignment for the standard-layout types produced from debug info). Cap at
  // 8 bytes, which matches the fundamental alignment on the supported targets.
  std::optional<uint64_t> byte_size = t->GetByteSize();
  if (!byte_size || *byte_size == 0)
    return std::nullopt;
  uint64_t align_bytes = 1;
  while (align_bytes * 2 <= 8 && (*byte_size % (align_bytes * 2)) == 0)
    align_bytes *= 2;
  return align_bytes * 8;
}

CompilerType TypeSystemCpp::GetBuiltinTypeByName(ConstString name) {
  llvm::StringRef name_ref = name.GetStringRef();

  // `_BitInt(N)` / `unsigned _BitInt(N)` are synthesized on demand (they never
  // appear as an enumerated builtin spelling). Mirror TypeSystemClang: parse the
  // bit width and lay the type out with the target's ABI size.
  bool bitint_unsigned = name_ref.consume_front("unsigned _BitInt(");
  if (bitint_unsigned || name_ref.consume_front("_BitInt(")) {
    uint64_t bits;
    if (name_ref.consumeInteger(/*Radix=*/10, bits))
      return CompilerType();
    if (name_ref != ")")
      return CompilerType();
    std::optional<uint64_t> byte_size =
        m_context.GetLanguageOpts().GetBitIntByteSize(bits);
    if (!byte_size)
      return CompilerType();
    return GetCompilerType(m_context.GetBuiltinType(
        name.GetStringRef(), *byte_size,
        bitint_unsigned ? lldb::eEncodingUint : lldb::eEncodingSint,
        bitint_unsigned ? lldb::eFormatUnsigned : lldb::eFormatDecimal));
  }

  // `__int128_t` / `__uint128_t` are aliases for the 128-bit integer builtins
  // (whose canonical spellings are `__int128` / `unsigned __int128`). They are
  // not themselves builtin spellings, so map them explicitly.
  if (name_ref == "__int128_t")
    return GetCompilerType(
        m_context.GetBuiltinType(cpp_typesystem::BuiltinKind::Int128));
  if (name_ref == "__uint128_t")
    return GetCompilerType(
        m_context.GetBuiltinType(cpp_typesystem::BuiltinKind::UnsignedInt128));

  if (cpp_typesystem::BuiltinType *bt =
          m_context.GetBuiltinTypeByName(name.GetStringRef()))
    return GetCompilerType(bt);
  return CompilerType();
}

CompilerType TypeSystemCpp::GetBasicTypeFromAST(BasicType basic_type) {
  using cpp_typesystem::BuiltinKind;
  // Map the language-neutral BasicType onto one of our enumerated builtin
  // kinds. Only the kinds TypeSystemCpp models are listed; anything else has
  // no basic type here.
  std::optional<BuiltinKind> kind;
  switch (basic_type) {
  case eBasicTypeVoid:
    kind = BuiltinKind::Void;
    break;
  case eBasicTypeBool:
    kind = BuiltinKind::Bool;
    break;
  case eBasicTypeChar:
    kind = BuiltinKind::Char;
    break;
  case eBasicTypeSignedChar:
    kind = BuiltinKind::SignedChar;
    break;
  case eBasicTypeUnsignedChar:
    kind = BuiltinKind::UnsignedChar;
    break;
  case eBasicTypeWChar:
    kind = BuiltinKind::WCharT;
    break;
  case eBasicTypeChar8:
    kind = BuiltinKind::Char8;
    break;
  case eBasicTypeChar16:
    kind = BuiltinKind::Char16;
    break;
  case eBasicTypeChar32:
    kind = BuiltinKind::Char32;
    break;
  case eBasicTypeShort:
    kind = BuiltinKind::Short;
    break;
  case eBasicTypeUnsignedShort:
    kind = BuiltinKind::UnsignedShort;
    break;
  case eBasicTypeInt:
    kind = BuiltinKind::Int;
    break;
  case eBasicTypeUnsignedInt:
    kind = BuiltinKind::UnsignedInt;
    break;
  case eBasicTypeLong:
    kind = BuiltinKind::Long;
    break;
  case eBasicTypeUnsignedLong:
    kind = BuiltinKind::UnsignedLong;
    break;
  case eBasicTypeLongLong:
    kind = BuiltinKind::LongLong;
    break;
  case eBasicTypeUnsignedLongLong:
    kind = BuiltinKind::UnsignedLongLong;
    break;
  case eBasicTypeInt128:
    kind = BuiltinKind::Int128;
    break;
  case eBasicTypeUnsignedInt128:
    kind = BuiltinKind::UnsignedInt128;
    break;
  case eBasicTypeFloat:
    kind = BuiltinKind::Float;
    break;
  case eBasicTypeDouble:
    kind = BuiltinKind::Double;
    break;
  case eBasicTypeLongDouble:
    kind = BuiltinKind::LongDouble;
    break;
  case eBasicTypeNullPtr:
    kind = BuiltinKind::NullPtr;
    break;
  case eBasicTypeObjCID:
  case eBasicTypeObjCClass: {
    // `id` / `Class` are typedefs over a pointer to the opaque `objc_object`
    // / `objc_class` record, matching how the DWARF parser and
    // ClangTypeConverter (see ConvertObjCObjectPointer) model them elsewhere
    // in TypeSystemCpp.
    const bool is_class = basic_type == eBasicTypeObjCClass;
    cpp_typesystem::Builder builder(*this);
    CompilerType opaque = builder.CreateRecordType(
        is_class ? "objc_class" : "objc_object",
        /*byte_size=*/std::nullopt, /*is_cpp_class=*/false,
        /*is_union=*/false);
    CompilerType ptr = builder.CreatePointerType(opaque);
    return builder.CreateTypedefType(is_class ? "Class" : "id", ptr);
  }
  case eBasicTypeObjCSel: {
    // `SEL` is a typedef over a pointer to the opaque `objc_selector` record.
    cpp_typesystem::Builder builder(*this);
    CompilerType opaque = builder.CreateRecordType(
        "objc_selector", /*byte_size=*/std::nullopt, /*is_cpp_class=*/false,
        /*is_union=*/false);
    CompilerType ptr = builder.CreatePointerType(opaque);
    return builder.CreateTypedefType("SEL", ptr);
  }
  default:
    break;
  }
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
}

CompilerType TypeSystemCpp::CreateGenericFunctionPrototype() {
  // An unprototyped `void ()` function type, used by ValueObjectVTable to give
  // a vtable slot that doesn't resolve to a known function a displayable
  // (hex address + description) function-pointer type. Mirrors
  // TypeSystemClang::CreateGenericFunctionPrototype's
  // ast.getFunctionNoProtoType(ast.VoidTy, ...): a variadic function with no
  // declared parameters is the closest match to "no prototype" in this
  // parameter-list-based model (there is no separate K&R/no-prototype bit).
  cpp_typesystem::Builder builder(*this);
  return builder.CreateFunctionType(builder.GetVoidType(),
                                    /*is_variadic=*/true);
}

CompilerType
TypeSystemCpp::GetBuiltinTypeForEncodingAndBitSize(Encoding encoding,
                                                   size_t bit_size) {
  using cpp_typesystem::BuiltinKind;
  const size_t byte_size = (bit_size + 7) / 8;
  std::optional<BuiltinKind> kind;
  switch (encoding) {
  case eEncodingSint:
    switch (byte_size) {
    case 1: kind = BuiltinKind::SignedChar; break;
    case 2: kind = BuiltinKind::Short; break;
    case 4: kind = BuiltinKind::Int; break;
    case 8: kind = BuiltinKind::LongLong; break;
    case 16: kind = BuiltinKind::Int128; break;
    }
    break;
  case eEncodingUint:
    switch (byte_size) {
    case 1: kind = BuiltinKind::UnsignedChar; break;
    case 2: kind = BuiltinKind::UnsignedShort; break;
    case 4: kind = BuiltinKind::UnsignedInt; break;
    case 8: kind = BuiltinKind::UnsignedLongLong; break;
    case 16: kind = BuiltinKind::UnsignedInt128; break;
    }
    break;
  case eEncodingIEEE754:
    switch (byte_size) {
    case 4: kind = BuiltinKind::Float; break;
    case 8: kind = BuiltinKind::Double; break;
    case 16: kind = BuiltinKind::LongDouble; break;
    }
    break;
  default:
    break;
  }
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
}

bool TypeSystemCpp::IsBeingDefined(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsConst(opaque_compiler_type_t type) {
  if (!type)
    return false;
  if (auto *cv =
          llvm::dyn_cast<cpp_typesystem::CVQualifiedType>(GetCppType(type)))
    return cv->IsConst();
  return false;
}

uint32_t TypeSystemCpp::IsHomogeneousAggregate(opaque_compiler_type_t type,
                                               CompilerType *base_type_ptr) {
  // Port of TypeSystemClang::IsHomogeneousAggregate: a Homogeneous
  // Floating-point/Vector Aggregate is a record with no base classes, that is
  // not a dynamic (polymorphic) class, and whose *direct* fields are all
  // either the same scalar floating-point type (HFA) or all the same vector
  // type with matching bit width (HVA) -- never a mix of the two, and never
  // any other kind of field (in particular, no recursion into nested
  // aggregate fields: a struct-typed field always disqualifies the record,
  // matching clang's field_qual_type->isFloatingType()/isVectorType() checks).
  if (!type)
    return 0;
  auto *record =
      llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(Desugar(GetCppType(type)));
  if (!record)
    return 0;
  if (!GetCompleteType(type))
    return 0;
  if (record->GetNumBaseClasses() > 0 || record->IsPolymorphic())
    return 0;

  uint32_t num_fields = 0;
  bool is_hva = false;
  bool is_hfa = false;
  cpp_typesystem::Type *base_type = nullptr;
  uint64_t base_bitwidth = 0;
  for (uint32_t i = 0, e = record->GetNumFields(); i != e; ++i) {
    const cpp_typesystem::Field *field = record->GetFieldAtIndex(i);
    if (!field || !field->type.Get())
      return 0;
    cpp_typesystem::Type *field_type = Desugar(field->type.Get());
    std::optional<uint64_t> field_bitwidth_opt = field_type->GetByteSize();
    uint64_t field_bitwidth = field_bitwidth_opt.value_or(0) * 8;

    if (field_type->GetEncoding() == lldb::eEncodingIEEE754) {
      if (num_fields == 0)
        base_type = field_type;
      else {
        if (is_hva)
          return 0;
        is_hfa = true;
        if (field_type != base_type)
          return 0;
      }
    } else if (auto *array =
                   llvm::dyn_cast<cpp_typesystem::ArrayType>(field_type);
               array && array->IsVector()) {
      if (num_fields == 0) {
        base_type = field_type;
        base_bitwidth = field_bitwidth;
      } else {
        if (is_hfa)
          return 0;
        is_hva = true;
        if (base_bitwidth != field_bitwidth)
          return 0;
        if (field_type != base_type)
          return 0;
      }
    } else
      return 0;
    ++num_fields;
  }
  if (num_fields == 0)
    return 0;
  if (base_type_ptr)
    *base_type_ptr = GetCompilerType(base_type);
  return num_fields;
}

bool TypeSystemCpp::IsPolymorphicClass(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // A forward-declared/incomplete polymorphic record doesn't get
  // SetRecordPolymorphic applied until DWARF completion runs, so complete
  // the type first (matching the other predicates in this file, e.g.
  // IsTemplateType / GetNumTemplateArguments), otherwise IsPolymorphic()
  // reads the not-yet-populated default of false.
  GetCompleteType(type);
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  return t && t->IsPolymorphic();
}

bool TypeSystemCpp::IsTypedefType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // Strip elaborated display sugar (e.g. a qualifier-spelled `GlobalTypedef::V`)
  // but stop at the typedef itself, mirroring TypeSystemClang's
  // RemoveWrappingTypes({Typedef}): the source spelling only affects display,
  // so a typedef named through a qualifier is still a typedef.
  cpp_typesystem::Type *t = GetCppType(type);
  while (auto *el = llvm::dyn_cast<cpp_typesystem::ElaboratedType>(t))
    t = el->GetUnderlyingType();
  return llvm::isa<cpp_typesystem::TypedefType>(t);
}

CompilerType TypeSystemCpp::GetTypedefedType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  cpp_typesystem::Type *t = GetCppType(type);
  while (auto *el = llvm::dyn_cast<cpp_typesystem::ElaboratedType>(t))
    t = el->GetUnderlyingType();
  if (auto *td = llvm::dyn_cast<cpp_typesystem::TypedefType>(t))
    return GetCompilerType(td->GetUnderlyingType());
  return CompilerType();
}

bool TypeSystemCpp::IsVectorType(opaque_compiler_type_t type,
                                 CompilerType *element_type, uint64_t *size) {
  if (!type)
    return false;
  cpp_typesystem::Type *t = Desugar(GetCppType(type));
  auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(t);
  if (!array || !array->IsVector())
    return false;
  if (element_type)
    *element_type = GetCompilerType(array->GetElementType());
  if (size) {
    if (std::optional<uint64_t> num = array->GetNumElements())
      *size = *num;
  }
  return true;
}

CompilerType
TypeSystemCpp::GetFullyUnqualifiedType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  return GetCompilerType(GetFullyUnqualifiedTypeImpl(GetCppType(type)));
}

cpp_typesystem::Type *
TypeSystemCpp::GetFullyUnqualifiedTypeImpl(cpp_typesystem::Type *t) {
  // Mirror TypeSystemClang::GetFullyUnqualifiedType: strip top-level
  // cv-qualifiers, and recurse through pointers/references/arrays so their
  // pointee/element is likewise fully unqualified (so `namesp::Virtual * const`
  // -> `namesp::Virtual *`, and `const char *` -> `char *`). Other sugar
  // (typedefs, elaborated spellings) is preserved. A reference has no
  // cv-qualifiers of its own, but its referent is unqualified.
  if (!t)
    return t;
  // Strip any stacked top-level cv-qualifiers.
  while (auto *cv = llvm::dyn_cast<cpp_typesystem::CVQualifiedType>(t))
    t = cv->GetUnderlyingType();

  cpp_typesystem::Builder builder(*this);
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    cpp_typesystem::Type *stripped = GetFullyUnqualifiedTypeImpl(pointee);
    if (stripped != pointee)
      return static_cast<cpp_typesystem::Type *>(
          builder
              .CreatePointerType(stripped ? GetCompilerType(stripped)
                                          : CompilerType())
              .GetOpaqueQualType());
    return t;
  }
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    cpp_typesystem::Type *stripped = GetFullyUnqualifiedTypeImpl(pointee);
    if (stripped && stripped != pointee)
      return static_cast<cpp_typesystem::Type *>(
          builder.CreateReferenceType(GetCompilerType(stripped), ref->IsRValue())
              .GetOpaqueQualType());
    return t;
  }
  if (auto *array = llvm::dyn_cast<cpp_typesystem::ArrayType>(t)) {
    cpp_typesystem::Type *element = array->GetElementType();
    cpp_typesystem::Type *stripped = GetFullyUnqualifiedTypeImpl(element);
    if (stripped && stripped != element)
      return static_cast<cpp_typesystem::Type *>(
          builder
              .CreateArrayType(GetCompilerType(stripped),
                               array->GetNumElements())
              .GetOpaqueQualType());
    return t;
  }
  return t;
}

CompilerType TypeSystemCpp::GetNonReferenceType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  // A reference may hide behind sugar (e.g. `typedef int &td_int_ref`), so look
  // through the sugar to find it -- matching IsReferenceType, which also
  // Desugars. If they disagreed (IsReferenceType true but this returning the
  // sugared type unchanged) a consumer that loops while IsReferenceType() holds
  // -- like FormatManager::GetPossibleMatches -- would recurse forever on a
  // typedef-of-reference. Once the reference is found, peel only the reference
  // (not the referent's own typedef/cv sugar), mirroring clang: the
  // non-reference type of `const int &` is `const int`, and of `td_int_ref` is
  // `int`.
  if (auto *ref =
          llvm::dyn_cast<cpp_typesystem::ReferenceType>(Desugar(GetCppType(type))))
    return GetCompilerType(ref->GetPointeeType());
  return CompilerType(weak_from_this(), type);
}

bool TypeSystemCpp::IsReferenceType(opaque_compiler_type_t type,
                                    CompilerType *pointee_type,
                                    bool *is_rvalue) {
  if (pointee_type)
    pointee_type->Clear();
  if (is_rvalue)
    *is_rvalue = false;
  auto *ref = llvm::dyn_cast_or_null<cpp_typesystem::ReferenceType>(
      type ? Desugar(GetCppType(type)) : nullptr);
  if (!ref)
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(ref->GetPointeeType());
  if (is_rvalue)
    *is_rvalue = ref->IsRValue();
  return true;
}

/// The record backing a class-template instantiation, or null if \p type is not
/// a (possibly sugared) record. Template arguments are populated during
/// completion, so callers must complete the type first.
static cpp_typesystem::RecordType *
GetRecordForTemplateArgs(cpp_typesystem::Type *type) {
  return llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(Desugar(type));
}

bool TypeSystemCpp::IsTemplateType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    return record->GetNumTemplateArguments() > 0;
  return false;
}

size_t TypeSystemCpp::GetNumTemplateArguments(opaque_compiler_type_t type,
                                              bool expand_pack) {
  if (!type)
    return 0;
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    return record->GetNumTemplateArguments();
  return 0;
}

lldb::TemplateArgumentKind
TypeSystemCpp::GetTemplateArgumentKind(opaque_compiler_type_t type, size_t idx,
                                       bool expand_pack) {
  if (!type)
    return eTemplateArgumentKindNull;
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    if (const cpp_typesystem::TemplateArgument *arg =
            record->GetTemplateArgumentAtIndex(idx))
      return arg->kind;
  return eTemplateArgumentKindNull;
}

CompilerType TypeSystemCpp::GetTypeTemplateArgument(opaque_compiler_type_t type,
                                                    size_t idx,
                                                    bool expand_pack) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    if (const cpp_typesystem::TemplateArgument *arg =
            record->GetTemplateArgumentAtIndex(idx)) {
      if (arg->kind != lldb::eTemplateArgumentKindType)
        return CompilerType();
      // A `void` type-kind argument (e.g. `coroutine_handle<void>`) has a null
      // TypeRef: DWARF encodes `void` by omitting DW_AT_type on the
      // DW_TAG_template_type_parameter DIE, and TypeRef's null state normally
      // means "no argument". Disambiguate using the kind (already confirmed
      // Type above) by returning the actual `void` builtin rather than an
      // invalid CompilerType, matching TypeSystemClang (whose `void`
      // QualType is always valid).
      if (!arg->type.Get())
        return cpp_typesystem::Builder(*this).GetVoidType();
      return GetCompilerType(arg->type.Get());
    }
  return CompilerType();
}

std::optional<CompilerType::IntegralTemplateArgument>
TypeSystemCpp::GetIntegralTemplateArgument(opaque_compiler_type_t type,
                                           size_t idx, bool expand_pack) {
  if (!type)
    return std::nullopt;
  GetCompleteType(type);
  auto *record = GetRecordForTemplateArgs(GetCppType(type));
  if (!record)
    return std::nullopt;
  const cpp_typesystem::TemplateArgument *arg =
      record->GetTemplateArgumentAtIndex(idx);
  if (!arg || arg->kind != eTemplateArgumentKindIntegral)
    return std::nullopt;
  // A pointer/reference-typed argument (e.g. `&temp1.member`) has no integral
  // value; report it as absent rather than a bogus scalar.
  if (cpp_typesystem::Type *arg_type = arg->type.Get())
    if (llvm::isa<cpp_typesystem::PointerType>(arg_type) ||
        llvm::isa<cpp_typesystem::ReferenceType>(arg_type))
      return std::nullopt;

  // Reconstruct the value with the argument type's signedness.
  Scalar value;
  if (arg->type && arg->type.Get()->GetEncoding() == eEncodingSint)
    value = static_cast<int64_t>(arg->integral_value);
  else
    value = arg->integral_value;
  return CompilerType::IntegralTemplateArgument{value,
                                                GetCompilerType(arg->type.Get())};
}

CompilerType
TypeSystemCpp::GetDirectNestedTypeWithName(opaque_compiler_type_t type,
                                           llvm::StringRef name) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  if (auto *record = GetRecordForTemplateArgs(GetCppType(type)))
    if (cpp_typesystem::Type *nested = record->GetNestedTypeWithName(name))
      return GetCompilerType(nested);
  return CompilerType();
}
