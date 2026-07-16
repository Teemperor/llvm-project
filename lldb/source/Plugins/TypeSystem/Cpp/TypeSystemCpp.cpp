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

  // The vtable pointer sits at the start of the (derived) object.
  ValueObject::AddrAndType addr = valobj->GetAddressOf();
  if (addr.address == LLDB_INVALID_ADDRESS ||
      addr.type != eAddressTypeLoad)
    return std::nullopt;
  lldb::addr_t obj_addr = addr.address;

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
// "(unnamed enum)" (see TagDecl::printName). Returns an empty string for any
// other (named or non-tag) type.
static std::string BuildUnnamedTagName(cpp_typesystem::Type *t) {
  using namespace cpp_typesystem;
  if (!t->GetName().GetName().empty() ||
      !t->GetUnqualifiedName().GetName().empty())
    return {};
  if (auto *rec = llvm::dyn_cast<RecordType>(t)) {
    if (rec->IsUnion())
      return "(unnamed union)";
    if (rec->IsClassKeyword())
      return "(unnamed class)";
    return "(unnamed struct)";
  }
  if (llvm::isa<EnumType>(t))
    return "(unnamed enum)";
  return {};
}

static std::string BuildDisplayName(cpp_typesystem::Type *t,
                                    bool hide_default_args = true);

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
                                     llvm::StringRef decl) {
  std::string ret = fn->GetReturnType() ? BuildDisplayName(fn->GetReturnType())
                                        : std::string("void");
  std::string params;
  for (uint32_t i = 0, e = fn->GetNumParameters(); i != e; ++i) {
    if (!params.empty())
      params += ", ";
    params += BuildDisplayName(fn->GetParameterAtIndex(i));
  }
  if (fn->IsVariadic())
    params += params.empty() ? "..." : ", ...";
  // An empty, prototyped parameter list is rendered as `()` (matching clang's
  // TypePrinter and TypeSystemClang) -- not `(void)`. An unprototyped (K&R)
  // function has a DW_TAG_unspecified_parameters child and is modelled as
  // variadic above, so it prints as `(...)`.
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
static std::string
BuildTemplateArgName(const cpp_typesystem::TemplateArgument &arg,
                     bool hide_default_args) {
  if (arg.kind == lldb::eTemplateArgumentKindType)
    return arg.type.Get()
               ? BuildDisplayName(arg.type.Get(), hide_default_args)
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
      std::string enum_name = BuildDisplayName(enum_type, hide_default_args);
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
      return BuildDisplayName(value_type, hide_default_args);
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
                                        bool hide_default_args) {
  std::string args;
  for (uint32_t i = 0; i < rec->GetNumTemplateArguments(); ++i) {
    const cpp_typesystem::TemplateArgument *arg =
        rec->GetTemplateArgumentAtIndex(i);
    if (hide_default_args && arg->is_default)
      continue;
    if (!args.empty())
      args += ", ";
    args += BuildTemplateArgName(*arg, hide_default_args);
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

// True if any of the record's non-type template arguments has an enum type.
// Such arguments are the one case where the DWARF producer's spelling of the
// instantiation name (`Foo<(EnumType)0>`) diverges from clang's
// (`Foo<EnumType::Member>`), so the name must be reconstructed for them.
static bool
HasEnumTypedTemplateArgument(const cpp_typesystem::RecordType &rec) {
  for (uint32_t i = 0, e = rec.GetNumTemplateArguments(); i != e; ++i) {
    const cpp_typesystem::TemplateArgument *arg =
        rec.GetTemplateArgumentAtIndex(i);
    if (arg->kind == lldb::eTemplateArgumentKindIntegral &&
        llvm::isa_and_nonnull<cpp_typesystem::EnumType>(arg->type.Get()))
      return true;
  }
  return false;
}

// Build a type's (simplified) display name from the type model: qualified with
// its declaring namespaces (inline ones skipped) and, for a class-template
// instantiation, its template arguments with defaulted ones hidden.
static std::string BuildDisplayName(cpp_typesystem::Type *t,
                                    bool hide_default_args) {
  using namespace cpp_typesystem;
  if (!t)
    return "";

  // Composite types have no name of their own; build them from their parts.
  if (llvm::isa<ArrayType>(t)) {
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
    return BuildDisplayName(cur, hide_default_args) + dims;
  }
  if (auto *ptr = llvm::dyn_cast<PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (auto *fn = llvm::dyn_cast_or_null<FunctionType>(pointee))
      return BuildFunctionName(fn, ptr->IsBlockPointer() ? "(^)" : "(*)");
    return (pointee ? BuildDisplayName(pointee, hide_default_args)
                    : std::string("void")) +
           " *";
  }
  if (auto *ref = llvm::dyn_cast<ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (auto *fn = llvm::dyn_cast_or_null<FunctionType>(pointee))
      return BuildFunctionName(fn, ref->IsRValue() ? "(&&)" : "(&)");
    return (pointee ? BuildDisplayName(pointee, hide_default_args)
                    : std::string("void")) +
           (ref->IsRValue() ? " &&" : " &");
  }
  if (auto *fn = llvm::dyn_cast<FunctionType>(t))
    return BuildFunctionName(fn, "");
  if (auto *cx = llvm::dyn_cast<ComplexType>(t)) {
    std::string element = cx->GetElementType()
                              ? BuildDisplayName(cx->GetElementType(),
                                                 hide_default_args)
                              : std::string("float");
    return "_Complex " + element;
  }
  if (auto *cv = llvm::dyn_cast<CVQualifiedType>(t)) {
    std::string result;
    if (cv->IsConst())
      result += "const ";
    if (cv->IsVolatile())
      result += "volatile ";
    return result + (cv->GetUnderlyingType()
                         ? BuildDisplayName(cv->GetUnderlyingType(),
                                            hide_default_args)
                         : "");
  }
  if (auto *pa = llvm::dyn_cast<PtrAuthType>(t)) {
    std::string underlying =
        BuildDisplayName(pa->GetUnderlyingType(), hide_default_args);
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
    // builtins have no unqualified name but do carry a stored name.
    if (std::string unnamed = BuildUnnamedTagName(t); !unnamed.empty())
      return unnamed;
    return t->GetName().GetName().str();
  }

  std::string result;
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
    result += BuildTemplateArgList(rec, hide_default_args);
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
  return ConstString();
}

CompilerType TypeSystemCpp::GetTypeForDecl(void *opaque_decl) {
  return CompilerType();
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
  return 0;
}

CompilerType
TypeSystemCpp::GetFunctionArgumentAtIndex(opaque_compiler_type_t type,
                                          const size_t index) {
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
  return false;
}

bool TypeSystemCpp::IsMemberDataPointerType(opaque_compiler_type_t type) {
  return false;
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
  switch (GetCppType(type)->GetEncoding()) {
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

bool TypeSystemCpp::IsVoidType(opaque_compiler_type_t type) { return false; }

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
  return t->IsComplete();
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
  return CompilerType();
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
// arguments as `(EnumType)0` rather than the enumerator name.
void TypeSystemCpp::CompleteTemplateInstantiationForName(
    cpp_typesystem::Type *t) {
  auto *rec = llvm::dyn_cast_or_null<cpp_typesystem::RecordType>(t);
  if (!rec || rec->IsComplete())
    return;
  if (!rec->GetName().GetName().contains('<'))
    return;
  GetCompleteType(rec);
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
    return ConstString(llvm::formatv("{0} *", pointee_name).str());
  }
  // References likewise: "<pointee> &" or "<pointee> &&".
  if (auto *ref = llvm::dyn_cast<cpp_typesystem::ReferenceType>(t)) {
    cpp_typesystem::Type *pointee = ref->GetPointeeType();
    if (llvm::isa_and_nonnull<cpp_typesystem::FunctionType>(pointee))
      return ConstString(BuildDisplayName(t));
    std::string pointee_name =
        pointee ? GetTypeName(pointee, BaseOnly).GetStringRef().str() : "void";
    return ConstString(
        llvm::formatv("{0} {1}", pointee_name, ref->IsRValue() ? "&&" : "&")
            .str());
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
    if (rec->IsTemplateInstantiation() &&
        HasEnumTypedTemplateArgument(*rec)) {
      llvm::StringRef unqualified = t->GetUnqualifiedName().GetName();
      std::string base = unqualified.substr(0, unqualified.find('<')).str();
      std::string args = BuildTemplateArgList(rec, /*hide_default_args=*/false);
      if (BaseOnly)
        return ConstString(base + args);
      // Fully-qualified: prefix the enclosing namespace/class scopes.
      std::string result;
      AppendNamespacePrefix(t->GetDeclContext(), result);
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
  // "(unnamed struct)" etc. to match clang / TypeSystemClang.
  if (std::string unnamed = BuildUnnamedTagName(t); !unnamed.empty())
    return ConstString(unnamed);
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
      if (!pointee ||
          !llvm::isa<cpp_typesystem::RecordType>(Desugar(pointee)))
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
  return -1;
}

CompilerType
TypeSystemCpp::GetFunctionArgumentTypeAtIndex(opaque_compiler_type_t type,
                                              size_t idx) {
  return CompilerType();
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
  return 0;
}

TypeMemberFunctionImpl
TypeSystemCpp::GetMemberFunctionAtIndex(opaque_compiler_type_t type,
                                        size_t idx) {
  return TypeMemberFunctionImpl();
}

CompilerType TypeSystemCpp::GetPointeeType(opaque_compiler_type_t type) {
  if (!type)
    return CompilerType();
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(
          Desugar(GetCppType(type))))
    return GetCompilerType(ptr->GetPointeeType());
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
  if (std::optional<uint64_t> byte_size = GetCppType(type)->GetByteSize())
    return *byte_size * 8;
  // Function types have no storage of their own. Matching TypeSystemClang
  // (clang models function types with a type size of 0), report a bit size of
  // 0 rather than an error. This keeps the dereferenced-value child of a
  // function pointer/reference from surfacing a size error (or a spurious byte
  // read) as its summary -- a zero-sized value simply has no value string.
  if (llvm::isa<cpp_typesystem::FunctionType>(Desugar(GetCppType(type))))
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
    return builder.GetBuiltinType(ConstString(name), size, e, f);
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
  case '#': // Class
  case ':': // SEL
    // Modeled as an opaque pointer; the ObjC object graph is not reconstructed.
    return builder.CreatePointerType(CompilerType());
  case '^': { // pointer to the following encoding
    CompilerType pointee = RealizeObjCEncoding(builder, enc);
    return builder.CreatePointerType(pointee);
  }
  default:
    return CompilerType();
  }
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
      builder.CreateObjCInterfaceType(class_name, std::nullopt);
  auto *iface = llvm::cast<cpp_typesystem::ObjCInterfaceType>(
      GetCppType(iface_ct.GetOpaqueQualType()));
  // Publish before filling so a self-referential ivar can't recurse forever.
  m_runtime_objc_types[class_name.GetStringRef()] = iface;

  descriptor->Describe(
      /*superclass_func=*/nullptr, /*instance_method_func=*/nullptr,
      /*class_method_func=*/nullptr,
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
              builder.GetBuiltinType(ConstString("char"), 1,
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
  // Only redirect a module interface that has no debug-info ivars; a type that
  // already has fields (a DWARF-described class, or a runtime-built scratch
  // type) is answered directly.
  if (!objc || objc->GetNumFields() != 0 || !exe_ctx)
    return CompilerType();
  Process *process = exe_ctx->GetProcessPtr();
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
  return scratch->CreateRuntimeObjCInterface(class_name, *process, *runtime);
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
  // A pointer's single child is the dereferenced value. Unlike a reference, a
  // pointer is NOT transparent: expanding `ptr` does not splice in the pointee's
  // members. Keeping this fixed (rather than forwarding to the pointee's members
  // once the pointee happens to be complete) makes the child layout stable
  // regardless of when the pointee is lazily completed -- otherwise a pointer
  // whose child count was computed while its pointee was still a forward
  // declaration (1 deref child) would silently disagree with a later member
  // lookup that completes the pointee. `void *` has no children.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (!pointee)
      return 0;
    // An Objective-C object is only ever accessed through a pointer (there is
    // no by-value ObjC object), so a pointer to an ObjC interface is treated as
    // transparent -- its children are the interface's ivars/superclass, matching
    // TypeSystemClang. This does not conflict with the lazy-completion concerns
    // that keep C/C++ pointers opaque, since an ObjC interface is never a
    // by-value type reached through a separate deref child.
    if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(Desugar(pointee)))
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
  return eBasicTypeInvalid;
}

uint32_t TypeSystemCpp::GetNumFields(opaque_compiler_type_t type) {
  if (!type)
    return 0;
  GetCompleteType(type);
  return GetCppType(type)->GetNumFields();
}

CompilerType TypeSystemCpp::GetFieldAtIndex(opaque_compiler_type_t type,
                                            size_t idx, std::string &name,
                                            uint64_t *bit_offset_ptr,
                                            uint32_t *bitfield_bit_size_ptr,
                                            bool *is_bitfield_ptr) {
  if (!type)
    return CompilerType();
  GetCompleteType(type);
  const Field *field = GetCppType(type)->GetFieldAtIndex(idx);
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

  // A pointer's single child is the dereferenced value. A pointer is NOT
  // transparent (unlike a reference, below): its members are reached by first
  // dereferencing it. Keeping this fixed makes the child layout independent of
  // when the pointee is lazily completed (see GetNumChildren), and keeps the
  // deref child that the DIL `ptr->member` path relies on.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (!pointee)
      return CompilerType(); // Can't dereference `void *`.

    // A pointer to an ObjC interface is transparent (see GetNumChildren): its
    // children are the interface's ivars/superclass, reached without an
    // intervening deref child. This matches TypeSystemClang and is what lets
    // `p obj` / the object-description fallback show the ivars.
    if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(Desugar(pointee))) {
      GetCompleteType(GetCompilerType(pointee).GetOpaqueQualType());
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
    if (std::optional<uint64_t> byte_size = pointee->GetByteSize())
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
  // A pointer is not transparent: its only child is the (unnamed) dereferenced
  // value, so no named member is directly addressable on the pointer. A
  // reference forwards to its aggregate referent.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    // A pointer to an ObjC interface is transparent (see GetNumChildren): its
    // named children are the interface's members, so forward the lookup.
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (pointee &&
        llvm::isa<cpp_typesystem::ObjCInterfaceType>(Desugar(pointee)))
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
  // A pointer or reference forwards member lookup to its (aggregate) pointee,
  // so that e.g. `ptr->member` / `ref.member` resolves against the pointed-to
  // record. A pointer is not transparent (see GetNumChildren): its single child
  // (index 0) is the dereferenced value, so the member is reached by stepping
  // through that deref child. This keeps the returned indices consistent with
  // GetNumChildren / GetChildCompilerTypeAtIndex regardless of whether the
  // pointee has been completed yet (completion is triggered lazily and must not
  // retroactively change the pointer's child layout). A reference stays
  // transparent, mirroring GetNumChildren.
  if (auto *ptr = llvm::dyn_cast<cpp_typesystem::PointerType>(t)) {
    cpp_typesystem::Type *pointee = ptr->GetPointeeType();
    if (!pointee || !pointee->IsAggregate())
      return 0;
    // A pointer to an Objective-C interface is transparent (see GetNumChildren
    // / GetChildCompilerTypeAtIndex): its children are the interface's members
    // directly, with no intervening deref child. Recurse without pushing the
    // index-0 deref step so the returned indices match the child layout.
    if (llvm::isa<cpp_typesystem::ObjCInterfaceType>(Desugar(pointee)))
      return GetIndexOfChildMemberWithNameImpl(
          static_cast<opaque_compiler_type_t>(pointee), name,
          omit_empty_base_classes, /*descend_anon_fields=*/true, child_indexes);
    child_indexes.push_back(0);
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
                          size_t byte_size) {
  lldb::offset_t offset = byte_offset;
  const bool is_signed = enum_type.IsSigned();
  const uint64_t enum_svalue =
      is_signed ? static_cast<uint64_t>(
                      data.GetMaxS64Bitfield(&offset, byte_size, 0, 0))
                : data.GetMaxU64Bitfield(&offset, byte_size, 0, 0);

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
    if (enumerator.value == enum_svalue) {
      s.PutCString(enumerator.name.GetName());
      return true;
    }
  }

  // Unsigned values make more sense for flags.
  offset = byte_offset;
  const uint64_t enum_uvalue =
      data.GetMaxU64Bitfield(&offset, byte_size, 0, 0);

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
    if ((format == eFormatEnum || format == eFormatDefault) &&
        bitfield_bit_size == 0)
      return DumpEnumValue(*enum_type, s, data, data_offset, data_byte_size);
  }
  return DumpDataExtractor(data, &s, data_offset, format, data_byte_size,
                           /*item_count=*/1, UINT32_MAX, LLDB_INVALID_ADDRESS,
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

void TypeSystemCpp::DumpTypeDescription(opaque_compiler_type_t type, Stream &s,
                                        DescriptionLevel level) {
  if (!type)
    return;
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

  if (auto *record = llvm::dyn_cast<cpp_typesystem::RecordType>(t)) {
    GetCompleteType(type);
    const char *tag =
        record->IsUnion()
            ? "union"
            : (llvm::isa<cpp_typesystem::ClassType>(record) ? "class"
                                                            : "struct");
    s.Printf("%s %s {\n", tag, GetTypeName(t, /*BaseOnly=*/false).GetCString());
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
  default:
    break;
  }
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
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
  return 0;
}

bool TypeSystemCpp::IsPolymorphicClass(opaque_compiler_type_t type) {
  return false;
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
  return false;
}

CompilerType
TypeSystemCpp::GetFullyUnqualifiedType(opaque_compiler_type_t type) {
  return CompilerType();
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
            record->GetTemplateArgumentAtIndex(idx))
      return GetCompilerType(arg->type.Get());
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
