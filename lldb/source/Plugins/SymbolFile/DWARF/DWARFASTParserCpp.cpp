//===-- DWARFASTParserCpp.cpp -----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFASTParserCpp.h"

#include "DWARFASTParser.h"
#include "DWARFDIE.h"
#include "DWARFDeclContext.h"
#include "DWARFDefines.h"
#include "DWARFUnit.h"
#include "SymbolFileDWARF.h"
#include "SymbolFileDWARFDebugMap.h"

#include "Plugins/TypeSystem/Cpp/Builder.h"
#include "Plugins/TypeSystem/Cpp/Namespace.h"
#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "Plugins/Language/ObjC/ObjCLanguage.h"

#include "lldb/Core/Declaration.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Expression/DWARFExpressionList.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/DWARF/DWARFTypePrinter.h"

#include <future>
#include <vector>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;
using namespace llvm::dwarf;

// Launch policy for resolving a record's referenced member/base types while
// completing it. Each future's type-system access is serialized through
// TypeSystemCpp's lock (see ResolveReferencedType), so that side is thread
// safe. The DWARF/SymbolFile side, however, is not: SymbolFileDWARF guards
// itself with the Module's recursive_mutex, which CompleteType already holds
// while CompleteTypeFromDWARF runs. A worker thread calling ResolveTypeUID
// would block trying to re-acquire that mutex (recursive_mutex only re-enters
// on the owning thread) while this thread waits on the worker -- a deadlock.
//
// So today the resolutions run with std::launch::deferred: each future
// executes inline, on this (lock-owning) thread, when its result is needed.
// The work is already structured as independent futures, so switching to
// std::launch::async to spread it across cores is a one-line change once the
// DWARF reads no longer require the Module mutex to be held across completion.
static constexpr std::launch kMemberResolutionPolicy = std::launch::deferred;

DWARFASTParserCpp::DWARFASTParserCpp(TypeSystemCpp &ts)
    : DWARFASTParser(Kind::DWARFASTParserCpp), m_ts(ts) {}

DWARFASTParserCpp::~DWARFASTParserCpp() = default;

Function *DWARFASTParserCpp::ParseFunctionFromDWARF(CompileUnit &comp_unit,
                                                    const DWARFDIE &die,
                                                    AddressRanges func_ranges) {
  if (die.Tag() != DW_TAG_subprogram)
    return nullptr;

  llvm::DWARFAddressRangesVector unused_func_ranges;
  const char *name = nullptr;
  const char *mangled = nullptr;
  std::optional<int> decl_file, decl_line, decl_column;
  std::optional<int> call_file, call_line, call_column;
  DWARFExpressionList frame_base;

  if (!die.GetDIENamesAndRanges(name, mangled, unused_func_ranges, decl_file,
                                decl_line, decl_column, call_file, call_line,
                                call_column, &frame_base))
    return nullptr;

  Mangled func_name;
  if (mangled && name &&
      Mangled::GetManglingScheme(mangled) == Mangled::eManglingSchemeNone) {
    // The linkage name is present but is not actually a mangled name. Display
    // the source name (DW_AT_name) and keep the linkage name as the symbol so
    // lookups by either name still resolve.
    func_name.SetDemangledName(ConstString(name));
    func_name.SetMangledName(ConstString(mangled));
  } else if (mangled) {
    func_name.SetValue(ConstString(mangled));
  } else if ((die.GetParent().Tag() == DW_TAG_compile_unit ||
              die.GetParent().Tag() == DW_TAG_partial_unit) &&
             Language::LanguageIsCPlusPlus(
                 SymbolFileDWARF::GetLanguage(*die.GetCU())) &&
             !Language::LanguageIsObjC(
                 SymbolFileDWARF::GetLanguage(*die.GetCU())) &&
             name && strcmp(name, "main") != 0) {
    // No mangled name in the DWARF: synthesize the demangled name from the decl
    // context (skipping `main`, which is never mangled), and keep DW_AT_name as
    // the symbol so a lookup by that name still resolves.
    func_name.SetDemangledName(ConstructDemangledNameFromDWARF(die));
    func_name.SetMangledName(ConstString(name));
  } else if (name) {
    func_name.SetValue(ConstString(name));
  }

  SymbolFileDWARF *dwarf = die.GetDWARF();
  // Supply the type _only_ if it has already been parsed.
  Type *func_type = dwarf->GetDIEToType().lookup(die.GetDIE());
  const user_id_t func_user_id = die.GetID();
  Address func_addr = func_ranges[0].GetBaseAddress();

  auto func_sp = std::make_shared<Function>(
      &comp_unit, func_user_id, func_user_id, func_name, func_type,
      std::move(func_addr), std::move(func_ranges));

  if (frame_base.IsValid())
    func_sp->GetFrameBaseExpression() = frame_base;
  comp_unit.AddFunction(func_sp);
  return func_sp.get();
}

/// Map a DWARF DW_AT_encoding value to an lldb::Encoding.
static lldb::Encoding GetEncodingFromDWARF(uint64_t dw_ate) {
  switch (dw_ate) {
  case DW_ATE_signed:
  case DW_ATE_signed_char:
    return eEncodingSint;
  case DW_ATE_boolean:
  case DW_ATE_unsigned:
  case DW_ATE_unsigned_char:
  // char8_t/char16_t/char32_t are unsigned; Clang emits them as DW_ATE_UTF
  // (and, historically, DW_ATE_UCS). Treat them as unsigned integers so they
  // match the canonical builtin instances.
  case DW_ATE_UTF:
  case DW_ATE_UCS:
    return eEncodingUint;
  case DW_ATE_float:
    return eEncodingIEEE754;
  default:
    return eEncodingInvalid;
  }
}

/// Map a DWARF DW_AT_encoding value to the format used to display the type's
/// values. This is only needed as a fallback: base types matching an
/// enumerated builtin get their format from TypeSystemCpp's canonical
/// instance. Char and boolean types share the integer encodings and so need
/// to be distinguished here (mirroring TypeSystemClang::GetFormat).
static lldb::Format GetFormatFromDWARF(uint64_t dw_ate) {
  switch (dw_ate) {
  case DW_ATE_signed_char:
  case DW_ATE_unsigned_char:
    return eFormatChar;
  case DW_ATE_boolean:
    return eFormatBoolean;
  case DW_ATE_signed:
    return eFormatDecimal;
  case DW_ATE_unsigned:
    return eFormatUnsigned;
  case DW_ATE_float:
    return eFormatFloat;
  default:
    return eFormatDefault;
  }
}

/// Build a populated lldb_private::Declaration (source file/line/column) for
/// \p die from its DW_AT_decl_file/DW_AT_decl_line/DW_AT_decl_column
/// attributes. Mirrors the generic handling in
/// DWARFASTParserClang::ParsedDWARFTypeAttributes: walk the *flattened*
/// attribute list from \p die.GetAttributes(), which also follows
/// DW_AT_specification/DW_AT_abstract_origin chains (so a subprogram that only
/// carries decl info on its out-of-line specification DIE still gets it
/// here), and resolve DW_AT_decl_file against the compile unit that attribute
/// actually came from (attributes.CompileUnitAtIndex(i)) rather than die's own
/// CU -- they can differ when DW_AT_specification uses DW_FORM_ref_addr to
/// point into another compile unit. Returns a default (empty) Declaration if
/// \p die is invalid or carries none of these attributes.
static Declaration GetDIEDeclaration(const DWARFDIE &die) {
  Declaration decl;
  if (!die)
    return decl;
  DWARFAttributes attributes = die.GetAttributes();
  for (size_t i = 0; i < attributes.Size(); ++i) {
    DWARFFormValue form_value;
    if (!attributes.ExtractFormValueAtIndex(i, form_value))
      continue;
    switch (attributes.AttributeAtIndex(i)) {
    case DW_AT_decl_file:
      if (DWARFUnit *cu = attributes.CompileUnitAtIndex(i))
        decl.SetFile(cu->GetFile(form_value.Unsigned()));
      break;
    case DW_AT_decl_line:
      decl.SetLine(form_value.Unsigned());
      break;
    case DW_AT_decl_column:
      decl.SetColumn(form_value.Unsigned());
      break;
    default:
      break;
    }
  }
  return decl;
}

TypeSP DWARFASTParserCpp::ParseBaseType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();
  ConstString name(die.GetName());
  // Mirror DWARFASTParserClang, which maps the DWARF `_Float16` base type onto
  // clang's half type, whose canonical spelling is `__fp16`. This keeps both
  // the type name and reconstructed template-argument display names consistent
  // with TypeSystemClang.
  if (name.GetStringRef() == "_Float16")
    name = ConstString("__fp16");
  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);
  uint64_t dw_ate = die.GetAttributeValueAsUnsigned(DW_AT_encoding, 0);
  lldb::Encoding encoding = GetEncodingFromDWARF(dw_ate);
  lldb::Format format = GetFormatFromDWARF(dw_ate);

  // A `_Complex` type: `_Complex float/double/long double`
  // (DW_ATE_complex_float) or the Clang extension `_Complex int` (emitted with
  // the vendor encoding DW_ATE_lo_user and a "complex" name). Model it as a
  // ComplexType over an element builtin of half the total size.
  const bool is_complex_float = dw_ate == DW_ATE_complex_float;
  const bool is_complex_int =
      dw_ate == DW_ATE_lo_user && name.GetStringRef().contains("complex");
  if ((is_complex_float || is_complex_int) && byte_size && *byte_size) {
    uint64_t element_bytes = *byte_size / 2;
    cpp_typesystem::Builder builder(m_ts);
    CompilerType element;
    if (is_complex_float)
      element = builder.GetBuiltinType(
          element_bytes == 4   ? "float"
          : element_bytes == 8 ? "double"
                               : "long double",
          element_bytes, lldb::eEncodingIEEE754, lldb::eFormatFloat);
    else {
      // The vendor complex-integer encoding doesn't record the element type, so
      // pick the signed integer builtin of the right size by name (LLDB can't
      // tell `long` from `long long` when they share a size -- see the FIXMEs in
      // the ComplexInt test).
      llvm::StringRef int_name = element_bytes == 1   ? "signed char"
                                 : element_bytes == 2 ? "short"
                                 : element_bytes == 4 ? "int"
                                 : element_bytes == 8 ? "long"
                                                      : "__int128";
      element = builder.GetBuiltinType(int_name, element_bytes,
                                       lldb::eEncodingSint, lldb::eFormatDecimal);
    }
    CompilerType complex_type = builder.CreateComplexType(element);
    Declaration decl = GetDIEDeclaration(die);
    return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                           LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                           complex_type, Type::ResolveState::Full);
  }

  // Map this base type onto the matching canonical builtin type, falling back
  // to a bespoke type (using the format derived above) if it is not one of the
  // enumerated builtins.
  CompilerType compiler_type = cpp_typesystem::Builder(m_ts).GetBuiltinType(
      name.GetStringRef(), byte_size, encoding, format);

  Declaration decl = GetDIEDeclaration(die);
  return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                         LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                         compiler_type, Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParseUnspecifiedType(const DWARFDIE &die) {
  // The only DW_TAG_unspecified_type LLDB models is `std::nullptr_t`
  // (a.k.a. `decltype(nullptr)`), which Clang emits with that name and no
  // DW_AT_type. Map it onto the canonical nullptr_t builtin.
  SymbolFileDWARF *dwarf = die.GetDWARF();
  ConstString name(die.GetName());
  llvm::StringRef n = name.GetStringRef();
  if (n != "decltype(nullptr)" && n != "std::nullptr_t" && n != "nullptr_t")
    return nullptr;

  const uint64_t ptr_size = m_ts.GetPointerByteSize();
  cpp_typesystem::Builder builder(m_ts);
  CompilerType compiler_type =
      builder.GetBuiltinType("decltype(nullptr)", ptr_size,
                             lldb::eEncodingUint, lldb::eFormatHex);
  Declaration decl = GetDIEDeclaration(die);
  return dwarf->MakeType(die.GetID(), name, ptr_size, /*context=*/nullptr,
                         LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                         compiler_type, Type::ResolveState::Full);
}

std::string DWARFASTParserCpp::GetDIEClassTemplateParams(DWARFDIE die) {
  if (DWARFDIE signature_die = die.GetReferencedDIE(DW_AT_signature))
    die = signature_die;

  // If the name already carries the template arguments (e.g. "Wrapper<int>"),
  // don't reconstruct and duplicate them.
  const char *name = die.GetName();
  if (name && llvm::StringRef(name).contains('<'))
    return {};

  // Reconstruct the "<...>" suffix from the DW_TAG_template_{type,value}_-
  // parameter children. This covers both type parameters (e.g. "int") and
  // non-type/value parameters (e.g. "3").
  std::string params;
  llvm::raw_string_ostream os(params);
  llvm::DWARFTypePrinter<DWARFDIE>(os).appendAndTerminateTemplateParameters(
      die);
  return params;
}

/// Emit the "Scope::" prefix for the DIE \p die by walking its ancestors,
/// mirroring llvm::DWARFTypePrinter::appendScopes but SKIPPING DW_TAG_module
/// scopes. In -gmodules debug info, types defined in a PCH/module are nested
/// under a DW_TAG_module scope (named e.g. "pch.h"); that scope is a debug-info
/// container, not a C++ decl context, so it must not appear in the type name
/// (otherwise "IntContainer" would print as "pch.h::IntContainer"). This
/// matches DWARFASTParserClang, which likewise ignores module scopes for naming.
static void AppendScopesSkippingModules(llvm::DWARFTypePrinter<DWARFDIE> &printer,
                                        llvm::raw_ostream &os, DWARFDIE die) {
  if (!die)
    return;
  switch (die.Tag()) {
  case DW_TAG_compile_unit:
  case DW_TAG_type_unit:
  case DW_TAG_skeleton_unit:
  case DW_TAG_subprogram:
  case DW_TAG_lexical_block:
    return;
  case DW_TAG_module:
    // Skip the module container entirely: don't emit it and continue up to its
    // parent so any real enclosing scopes are still printed.
    AppendScopesSkippingModules(printer, os, die.GetParent());
    return;
  default:
    break;
  }
  AppendScopesSkippingModules(printer, os, die.GetParent());
  printer.appendUnqualifiedName(die);
  os << "::";
}

/// The fully-qualified name of a named type DIE, including enclosing namespace
/// and class scopes (e.g. "std::__1::vector<int, std::__1::allocator<int> >")
/// and reconstructed template arguments. Producing the scoped spelling is what
/// lets type-name-based data formatters (such as libc++'s container summaries)
/// match our types.
static std::string GetDIEQualifiedName(const DWARFDIE &die) {
  // A DIE with no DW_AT_name is an unnamed type (e.g. a function-local unnamed
  // struct/union/enum). llvm::DWARFTypePrinter::appendQualifiedName would emit
  // a bogus bare tag keyword (e.g. "structure ") for it, so return an empty
  // name and let the type system supply an "(unnamed ...)" display name.
  if (!die.GetName())
    return std::string();
  std::string name;
  llvm::raw_string_ostream os(name);
  llvm::DWARFTypePrinter<DWARFDIE> printer(os);
  // Reimplement appendQualifiedName here so we can skip DW_TAG_module scopes
  // (see AppendScopesSkippingModules); the upstream printer would include them.
  AppendScopesSkippingModules(printer, os, die.GetParent());
  printer.appendUnqualifiedName(die);
  return name;
}

ConstString
DWARFASTParserCpp::ConstructDemangledNameFromDWARF(const DWARFDIE &die) {
  // TypeSystemCpp must not touch clang decl contexts, so (unlike
  // DWARFASTParserClang) build the demangled name straight from DWARF: the
  // qualified function name followed by its parameter type spellings and any
  // cv-qualifier on the object parameter.
  // Use the DWARF decl context's qualified name (mirroring
  // DWARFASTParserClang) rather than GetDIEQualifiedName: the former prefixes
  // top-level names with "::" (e.g. "::foo"), which is what the demangled
  // function name is expected to carry.
  StreamString sstr;
  sstr << die.GetDWARFDeclContext().GetQualifiedName();

  bool is_variadic = false;
  bool is_const = false;
  std::vector<std::string> param_types;
  for (DWARFDIE child : die.children()) {
    switch (child.Tag()) {
    case DW_TAG_formal_parameter: {
      DWARFDIE param_type_die =
          child.GetAttributeValueAsReferenceDIE(DW_AT_type);
      // The artificial `this` parameter isn't part of the printed signature,
      // but a `const` method has a `this` whose pointee is const-qualified.
      if (child.GetAttributeValueAsUnsigned(DW_AT_artificial, 0)) {
        if (Type *this_type = die.ResolveTypeUID(param_type_die)) {
          CompilerType pointee =
              this_type->GetForwardCompilerType().GetPointeeType();
          if (pointee.GetTypeQualifiers() & 0x1)
            is_const = true;
        }
        break;
      }
      std::string type_name;
      if (Type *param_type = die.ResolveTypeUID(param_type_die))
        type_name = param_type->GetForwardCompilerType().GetTypeName().GetString();
      param_types.push_back(std::move(type_name));
      break;
    }
    case DW_TAG_unspecified_parameters:
      is_variadic = true;
      break;
    default:
      break;
    }
  }

  sstr << "(";
  for (size_t i = 0; i < param_types.size(); ++i) {
    if (i > 0)
      sstr << ", ";
    sstr << param_types[i];
  }
  if (is_variadic) {
    if (!param_types.empty())
      sstr << ", ";
    sstr << "...";
  }
  sstr << ")";
  if (is_const)
    sstr << " const";

  return ConstString(sstr.GetString());
}

/// Build the chain of enclosing namespaces for \p die, marking libc++-style
/// inline namespaces (DWARF's DW_AT_export_symbols) so they can be skipped when
/// printing the type name. Returns the innermost namespace, or null if the type
/// is not (directly) inside a namespace.
static const cpp_typesystem::Namespace *
BuildDeclNamespace(const DWARFDIE &die, cpp_typesystem::Builder &builder) {
  llvm::SmallVector<DWARFDIE, 4> namespaces;
  for (DWARFDIE parent = die.GetParent(); parent; parent = parent.GetParent()) {
    // Only namespace scopes are modelled here; stop at the CU or any other
    // (e.g. record) scope.
    if (parent.Tag() != DW_TAG_namespace)
      break;
    namespaces.push_back(parent);
  }
  // Build outermost-first so each namespace can reference its parent.
  const cpp_typesystem::Namespace *ns = nullptr;
  for (DWARFDIE ns_die : llvm::reverse(namespaces)) {
    const char *ns_name = ns_die.GetName();
    bool is_inline =
        ns_die.GetAttributeValueAsUnsigned(DW_AT_export_symbols, 0) != 0;
    ns = builder.GetNamespace(ns_name ? ns_name : "", ns, is_inline);
  }
  return ns;
}

/// Build the interned Namespace for a DW_TAG_namespace DIE \p ns_die itself
/// (i.e. including \p ns_die, not just its parents). Returns null if \p ns_die
/// is not a namespace.
static const cpp_typesystem::Namespace *
BuildNamespaceForDIE(const DWARFDIE &ns_die, cpp_typesystem::Builder &builder) {
  if (!ns_die || ns_die.Tag() != DW_TAG_namespace)
    return nullptr;
  const cpp_typesystem::Namespace *parent =
      BuildDeclNamespace(ns_die, builder);
  const char *name = ns_die.GetName();
  bool is_inline =
      ns_die.GetAttributeValueAsUnsigned(DW_AT_export_symbols, 0) != 0;
  return builder.GetNamespace(name ? name : "", parent, is_inline);
}

/// The unqualified spelling of a named type DIE, including any reconstructed
/// template arguments (e.g. "FwdTemplateClass<int>"). This mirrors
/// GetDIEQualifiedName but omits the enclosing namespace/class scopes, since
/// TypeSystemCpp stores the scope separately (via SetDeclContext) and rebuilds
/// the qualified display name itself.
///
/// For a non-template type, or a type whose DW_AT_name already carries the
/// "<...>" template arguments, this yields the same string as the bare
/// DW_AT_name; it only differs for a template specialization whose DW_AT_name
/// is just the template's base name (e.g. a forward-declared
/// `FwdTemplateClass<int>`, whose declaration DIE is named "FwdTemplateClass"
/// with separate DW_TAG_template_*_parameter children).
static std::string GetDIEUnqualifiedName(const DWARFDIE &die) {
  // If the DW_AT_name already carries the template arguments, use it verbatim
  // so we don't reconstruct and duplicate them.
  const char *name = die.GetName();
  if (name && llvm::StringRef(name).contains('<'))
    return name;

  std::string result;
  llvm::raw_string_ostream os(result);
  llvm::DWARFTypePrinter<DWARFDIE>(os).appendUnqualifiedName(die);
  return result;
}

/// Record on \p type the namespace it is declared in and its unqualified
/// DW_AT_name spelling, so TypeSystemCpp can rebuild the display name while
/// hiding inline namespaces.
static void SetTypeNameInfo(const DWARFDIE &die, CompilerType type,
                            cpp_typesystem::Builder &builder) {
  builder.SetDeclContext(type, BuildDeclNamespace(die, builder));
  if (die.GetName())
    builder.SetUnqualifiedName(type, GetDIEUnqualifiedName(die));
}

CompilerDeclContext
DWARFASTParserCpp::GetDeclContextForUIDFromDWARF(const DWARFDIE &die) {
  // The decl context *of* a DIE. For a namespace DIE that is the namespace
  // itself (this backs SymbolFileDWARF::FindNamespace, which the expression
  // evaluator uses to discover a C++ namespace by name); for any other entity
  // (e.g. a function, so an expression evaluated in a namespaced frame can
  // resolve unqualified names in that namespace) it is the enclosing namespace.
  // The wrapped opaque pointer is the interned cpp_typesystem::Namespace.
  cpp_typesystem::Builder builder(m_ts);
  const cpp_typesystem::Namespace *ns =
      die.Tag() == DW_TAG_namespace ? BuildNamespaceForDIE(die, builder)
                                    : BuildDeclNamespace(die, builder);
  if (!ns)
    return CompilerDeclContext();
  return CompilerDeclContext(&m_ts,
                             const_cast<cpp_typesystem::Namespace *>(ns));
}

CompilerDeclContext
DWARFASTParserCpp::GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) {
  // The containing context of a DIE is the innermost enclosing namespace (used
  // by DIEInDeclContext to check whether a candidate namespace lives in a given
  // parent scope). A null chain means the global namespace, which has no valid
  // CompilerDeclContext.
  cpp_typesystem::Builder builder(m_ts);
  const cpp_typesystem::Namespace *ns = BuildDeclNamespace(die, builder);
  if (!ns)
    return CompilerDeclContext();
  return CompilerDeclContext(&m_ts,
                             const_cast<cpp_typesystem::Namespace *>(ns));
}

void DWARFASTParserCpp::CollectUsingDirectiveNamespaces(
    const DWARFDIE &block_die,
    std::vector<CompilerDeclContext> &namespaces) {
  cpp_typesystem::Builder builder(m_ts);
  // Walk the block and its enclosing lexical blocks / function, innermost
  // first, collecting each `using namespace N;` (DW_TAG_imported_module).
  for (DWARFDIE scope = block_die; scope; scope = scope.GetParent()) {
    for (DWARFDIE child : scope.children()) {
      if (child.Tag() != DW_TAG_imported_module)
        continue;
      DWARFDIE imported = child.GetAttributeValueAsReferenceDIE(DW_AT_import);
      const cpp_typesystem::Namespace *ns =
          BuildNamespaceForDIE(imported, builder);
      if (ns)
        namespaces.emplace_back(&m_ts,
                                const_cast<cpp_typesystem::Namespace *>(ns));
    }
    // Stop once we've processed the function scope; file-scope using-directives
    // are handled by the ordinary global search.
    if (scope.Tag() == DW_TAG_subprogram)
      break;
  }
}

void DWARFASTParserCpp::CollectUsingDeclarations(
    const DWARFDIE &block_die,
    std::vector<std::pair<ConstString, CompilerDeclContext>> &decls) {
  cpp_typesystem::Builder builder(m_ts);
  // Walk the block and its enclosing lexical blocks / function, innermost
  // first, collecting each `using ns::name;` (DW_TAG_imported_declaration).
  for (DWARFDIE scope = block_die; scope; scope = scope.GetParent()) {
    for (DWARFDIE child : scope.children()) {
      if (child.Tag() != DW_TAG_imported_declaration)
        continue;
      DWARFDIE imported = child.GetAttributeValueAsReferenceDIE(DW_AT_import);
      if (!imported)
        continue;
      const char *name = imported.GetName();
      if (!name || !name[0])
        continue;
      // The namespace the imported entity lives in is the innermost enclosing
      // namespace of the imported DIE (e.g. `Single` for `Single::single`).
      const cpp_typesystem::Namespace *ns =
          BuildDeclNamespace(imported, builder);
      if (!ns)
        continue;
      decls.emplace_back(
          ConstString(name),
          CompilerDeclContext(&m_ts,
                              const_cast<cpp_typesystem::Namespace *>(ns)));
    }
    // Stop once we've processed the function scope; file-scope using-declarations
    // are handled by the ordinary global search.
    if (scope.Tag() == DW_TAG_subprogram)
      break;
  }
}

CompilerType DWARFASTParserCpp::GetOwningClassForFunctionFromDWARF(
    const DWARFDIE &function_die) {
  if (!function_die)
    return CompilerType();

  // The semantic parent of a member-function DIE (following
  // DW_AT_specification / DW_AT_abstract_origin for out-of-line definitions) is
  // the class/struct/union it belongs to. This holds for static member
  // functions too, which have no `this` pointer to derive the class from.
  DWARFDIE parent = function_die.GetParentDeclContextDIE();
  if (!parent)
    return CompilerType();
  switch (parent.Tag()) {
  case DW_TAG_structure_type:
  case DW_TAG_class_type:
  case DW_TAG_union_type:
    break;
  default:
    return CompilerType();
  }

  Type *class_type = function_die.ResolveTypeUID(parent);
  if (!class_type)
    return CompilerType();
  return class_type->GetForwardCompilerType();
}

/// the JIT can resolve the call to the right symbol in the inferior. Mirrors
/// DWARFASTParserClang's MakeLLDBFuncAsmLabel.
static std::string MakeFuncAsmLabel(const DWARFDIE &die) {
  const char *name = die.GetMangledName(/*substitute_name_allowed=*/false);
  if (!name)
    return {};
  SymbolFileDWARF *dwarf = die.GetDWARF();
  if (!dwarf)
    return {};

  auto get_module_id = [&](SymbolFile *sym) -> lldb::user_id_t {
    if (!sym)
      return LLDB_INVALID_UID;
    ObjectFile *obj = sym->GetMainObjectFile();
    if (!obj)
      return LLDB_INVALID_UID;
    lldb::ModuleSP module_sp = obj->GetModule();
    return module_sp ? module_sp->GetID() : LLDB_INVALID_UID;
  };

  lldb::user_id_t module_id = get_module_id(dwarf->GetDebugMapSymfile());
  if (module_id == LLDB_INVALID_UID)
    module_id = get_module_id(dwarf);
  if (module_id == LLDB_INVALID_UID)
    return {};

  const lldb::user_id_t die_id = die.GetID();
  if (die_id == LLDB_INVALID_UID)
    return {};

  return FunctionCallLabel{/*discriminator=*/{}, module_id, die_id, name}
      .toString();
}

/// Return the DW_TAG_module DIE enclosing \p die (the top-most one), or an
/// invalid DIE if \p die is not scoped inside a Clang module. Mirrors
/// DWARFASTParserClang's GetContainingClangModuleDIE.
static DWARFDIE GetContainingClangModuleDIE(const DWARFDIE &die) {
  DWARFDIE top_module_die;
  for (DWARFDIE parent = die.GetParent(); parent;
       parent = parent.GetParent()) {
    const dw_tag_t tag = parent.Tag();
    if (tag == DW_TAG_module)
      top_module_die = parent;
    else if (tag == DW_TAG_compile_unit || tag == DW_TAG_partial_unit)
      break;
  }
  return top_module_die;
}

/// If \p die is a forward declaration nested inside a DW_TAG_module (a
/// -gmodules/PCH container), find the complete definition of the type in the
/// external module/PCH DWARF and return the (already parsed, complete) type for
/// it. Returns null if \p die is not a module forward declaration or no
/// definition can be found. Mirrors
/// DWARFASTParserClang::ParseTypeFromClangModule.
TypeSP DWARFASTParserCpp::FindClangModuleDefinitionType(const DWARFDIE &die) {
  DWARFDIE module_die = GetContainingClangModuleDIE(die);
  if (!module_die)
    return TypeSP();

  SymbolFileDWARF *dwarf = die.GetDWARF();
  if (!dwarf)
    return TypeSP();

  const char *module_name = module_die.GetName();
  if (!module_name)
    return TypeSP();

  // The complete definition lives in the .pcm/.pch DWARF referenced by this
  // module. Look it up there (and in the modules it imports) by decl context.
  ModuleSP clang_module_sp =
      dwarf->GetExternalModule(ConstString(module_name));
  if (!clang_module_sp)
    return TypeSP();

  std::vector<CompilerContext> die_context = die.GetDeclContext();
  TypeQuery query(die_context, TypeQueryOptions::e_module_search |
                                   TypeQueryOptions::e_find_one);
  query.AddLanguage(SymbolFileDWARF::GetLanguageFamily(*die.GetCU()));

  TypeResults results;
  clang_module_sp->FindTypes(query, results);
  TypeSP pcm_type_sp = results.GetTypeMap().FirstType();
  if (!pcm_type_sp) {
    // Also search modules imported by this symbol file (the definition may be
    // in a transitively imported module).
    results.AlreadySearched(dwarf);
    if (CompileUnit *cu = die.GetCU()->GetLLDBCompUnit())
      dwarf->ForEachExternalModule(
          *cu, results.GetSearchedSymbolFiles(), [&](Module &module) {
            module.FindTypes(query, results);
            pcm_type_sp = results.GetTypeMap().FirstType();
            return (bool)pcm_type_sp;
          });
  }

  if (!pcm_type_sp)
    return TypeSP();

  // Reuse the same TypeSP for this DIE so callers see the complete definition.
  // (Its members/bases are parsed lazily from the module's own DWARF.)
  dwarf->GetDIEToType()[die.GetDIE()] = pcm_type_sp.get();
  return pcm_type_sp;
}

TypeSP DWARFASTParserCpp::ParseStructureType(const DWARFDIE &die) {
  // A record that is only forward-declared inside a DW_TAG_module (a
  // -gmodules/PCH debug-info container) has its complete definition in the
  // separate module/PCH DWARF. Find that definition and use it, so the record
  // is completable on the frame-variable / value-object path too (the
  // expression evaluator finds the cross-module definition separately). This
  // mirrors DWARFASTParserClang::ParseTypeFromClangModule.
  if (die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0))
    if (TypeSP module_def = FindClangModuleDefinitionType(die))
      return module_def;

  SymbolFileDWARF *dwarf = die.GetDWARF();

  // Record the fully-qualified, template-argument-bearing spelling of the type
  // (e.g. the instantiation "Wrapper<int>" or "std::__1::vector<int, ...>")
  // rather than the bare DW_AT_name ("Wrapper" / "vector").
  ConstString name(GetDIEQualifiedName(die));
  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);

  // An Objective-C class (`@interface`) carries DW_AT_APPLE_runtime_class. It
  // is modeled by a dedicated ObjCInterfaceType (ivars are added as fields, the
  // superclass as its one base class) rather than a C/C++ record.
  const bool is_objc_class =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_APPLE_runtime_class)
          .has_value();
  if (is_objc_class) {
    // A CU that merely *uses* an ObjC class emits an incomplete stub for it
    // (no ivars, and no DW_AT_APPLE_objc_complete_type); the real definition
    // with ivars lives in the CU that implements the class. When this DIE is
    // such a stub, find and return the complete definition instead (mirroring
    // DWARFASTParserClang).
    const bool is_complete_objc_class =
        die.GetAttributeValueAsUnsigned(DW_AT_APPLE_objc_complete_type, 0) != 0;
    if (!is_complete_objc_class && name) {
      TypeSP complete =
          dwarf->FindCompleteObjCDefinitionTypeForDIE(die, name, true);
      if (!complete)
        if (SymbolFileDWARFDebugMap *debug_map = dwarf->GetDebugMapSymfile())
          complete =
              debug_map->FindCompleteObjCDefinitionTypeForDIE(die, name, true);
      if (complete)
        return complete;
    }

    CompilerType compiler_type;
    {
      cpp_typesystem::Builder builder(m_ts);
      compiler_type = builder.CreateObjCInterfaceType(name.GetStringRef(), byte_size);
      SetTypeNameInfo(die, compiler_type, builder);
    }

    Declaration decl = GetDIEDeclaration(die);
    TypeSP type_sp = dwarf->MakeType(
        die.GetID(), name, byte_size, /*context=*/nullptr, LLDB_INVALID_UID,
        Type::eEncodingIsUID, decl, compiler_type, Type::ResolveState::Forward);
    if (std::optional<DIERef> die_ref = die.GetDIERef())
      dwarf->GetForwardDeclCompilerTypeToDIE().insert_or_assign(
          compiler_type.GetOpaqueQualType(), *die_ref);
    return type_sp;
  }

  // In a C++ translation unit even a `struct` may have base classes, so back
  // records with a ClassType (which reserves storage for that C++-only
  // information). Plain C records use the lighter StructType.
  lldb::LanguageType language = SymbolFileDWARF::GetLanguage(*die.GetCU());
  bool is_cpp_class = Language::LanguageIsCPlusPlus(language);
  bool is_union = die.Tag() == DW_TAG_union_type;
  // Both `struct` and `class` are modeled by the same record type, so remember
  // the source keyword to reproduce it in display names (e.g. for an unnamed
  // record: "(unnamed struct)" vs "(unnamed class)").
  bool is_class_keyword = die.Tag() == DW_TAG_class_type;

  CompilerType compiler_type;
  {
    cpp_typesystem::Builder builder(m_ts);
    compiler_type =
        builder.CreateRecordType(name.GetStringRef(), byte_size, is_cpp_class,
                                 is_union, is_class_keyword);
    SetTypeNameInfo(die, compiler_type, builder);
  }

  Declaration decl = GetDIEDeclaration(die);
  // The lldb_private::Type record (unlike the cpp_typesystem::Type above) is
  // named with the bare, unqualified DW_AT_name spelling -- e.g. "valarray"
  // rather than "std::__1::valarray<int>" -- to match TypeSystemClang (whose
  // Type::m_name is the raw DW_AT_name), since e.g. DIL's out-of-bounds-index
  // diagnostic prints this name and is tested against that unqualified form.
  ConstString unqualified_name(GetDIEUnqualifiedName(die));
  TypeSP type_sp = dwarf->MakeType(
      die.GetID(), unqualified_name, byte_size, /*context=*/nullptr,
      LLDB_INVALID_UID, Type::eEncodingIsUID, decl, compiler_type,
      Type::ResolveState::Forward);

  // Remember which DIE backs this (so far incomplete) record so that
  // SymbolFileDWARF::CompleteType can find it and call back into
  // CompleteTypeFromDWARF when the members are needed.
  if (std::optional<DIERef> die_ref = die.GetDIERef())
    dwarf->GetForwardDeclCompilerTypeToDIE().insert_or_assign(
        compiler_type.GetOpaqueQualType(), *die_ref);

  return type_sp;
}

TypeSP DWARFASTParserCpp::ParseArrayType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  DWARFDIE element_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  Type *element_type = die.ResolveTypeUID(element_die);
  if (!element_type)
    return nullptr;

  // A DWARF array can describe multiple dimensions via several subrange
  // children. Build them from the innermost dimension outward so that e.g.
  // `int[2][3]` becomes an array-of-2 of array-of-3 of int.
  std::optional<SymbolFile::ArrayInfo> array_info = ParseChildArrayInfo(die);

  CompilerType array_type = element_type->GetForwardCompilerType();
  bool is_vector = die.GetAttributeValueAsUnsigned(DW_AT_GNU_vector, 0) != 0;
  {
    // Build the (possibly nested) array type(s) under a single lock.
    cpp_typesystem::Builder ts(m_ts);
    if (array_info && !array_info->element_orders.empty()) {
      for (auto it = array_info->element_orders.rbegin(),
                end = array_info->element_orders.rend();
           it != end; ++it)
        array_type = ts.CreateArrayType(array_type, *it);
    } else {
      // No bound information; model it as an array of unknown length.
      array_type = ts.CreateArrayType(array_type, std::nullopt);
    }
    // Remember the backing DIE on the outermost array so a runtime
    // (variable-length) bound can be re-resolved later (see GetNumChildren).
    if (auto *arr = llvm::dyn_cast_or_null<cpp_typesystem::ArrayType>(
            TypeSystemCpp::GetCppType(array_type.GetOpaqueQualType()))) {
      arr->SetDIEUID(die.GetID());
      // A GCC/Clang vector type (e.g. `float __attribute__((ext_vector_type(4)))`)
      // is described as an array with DW_AT_GNU_vector; mark it so LLDB treats
      // it as a vector type for formatting.
      arr->SetIsVector(is_vector);
    }
  }

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(array_type.GetByteSize(nullptr));

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, element_die.GetID(),
                         Type::eEncodingIsUID, decl, array_type,
                         Type::ResolveState::Full);
}

CompilerType DWARFASTParserCpp::ParseBlockFunctionType(const DWARFDIE &die) {
  // An Apple "blocks" pointer points at a compiler-generated literal struct
  // (marked with DW_AT_APPLE_block) that contains a `__FuncPtr` member: a
  // pointer to the block's invoke function. Follow that member to recover the
  // block's function type.
  if (!die || !die.GetAttributeValueAsUnsigned(DW_AT_APPLE_block, 0))
    return CompilerType();

  for (DWARFDIE child : die.children()) {
    if (child.Tag() != DW_TAG_member)
      continue;
    const char *member_name = child.GetAttributeValueAsString(DW_AT_name, "");
    if (!member_name || strcmp(member_name, "__FuncPtr") != 0)
      continue;

    DWARFDIE func_ptr_type = child.GetAttributeValueAsReferenceDIE(DW_AT_type);
    if (!func_ptr_type)
      return CompilerType();
    DWARFDIE func_type = func_ptr_type.GetAttributeValueAsReferenceDIE(DW_AT_type);
    if (!func_type)
      return CompilerType();
    if (Type *function = die.ResolveTypeUID(func_type))
      return function->GetForwardCompilerType();
    return CompilerType();
  }
  return CompilerType();
}

TypeSP DWARFASTParserCpp::ParsePointerType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);

  // Resolve the pointee type. A missing DW_AT_type means `void *`, in which
  // case the pointee stays empty. A pointer may point to an incomplete type,
  // so use the forward-declared CompilerType and don't force completion here.
  DWARFDIE pointee_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType pointee_type;
  bool is_block = false;
  if (pointee_die) {
    // An Apple "blocks" pointer (`int (^)(int)`) points at a block-literal
    // struct; model it as a pointer to the block's function type, flagged as a
    // block pointer, rather than a pointer to the literal struct.
    if (CompilerType block_func = ParseBlockFunctionType(pointee_die)) {
      pointee_type = block_func;
      is_block = true;
    } else if (Type *pointee = die.ResolveTypeUID(pointee_die)) {
      pointee_type = pointee->GetForwardCompilerType();
    }
  }

  CompilerType pointer_type =
      cpp_typesystem::Builder(m_ts).CreatePointerType(pointee_type, is_block);

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, pointee_die.GetID(),
                         Type::eEncodingIsUID, decl, pointer_type,
                         Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParseReferenceType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);

  // Resolve the referenced type. A reference may refer to an incomplete type,
  // so use the forward-declared CompilerType and don't force completion here. A
  // reference must refer to something (there is no `void &`), so a missing or
  // unresolvable referent means we can't build this type.
  DWARFDIE pointee_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  if (!pointee_die)
    return nullptr;
  Type *pointee = die.ResolveTypeUID(pointee_die);
  if (!pointee)
    return nullptr;
  CompilerType pointee_type = pointee->GetForwardCompilerType();

  bool is_rvalue = die.Tag() == DW_TAG_rvalue_reference_type;
  CompilerType reference_type =
      cpp_typesystem::Builder(m_ts).CreateReferenceType(pointee_type,
                                                        is_rvalue);

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, pointee_die.GetID(),
                         Type::eEncodingIsUID, decl, reference_type,
                         Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParsePointerToMemberType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  // A pointer-to-member always points at a member (data member or member
  // function) of a class, and both must be resolvable to build this type;
  // clang does not reliably emit DW_AT_byte_size for this DIE (it is
  // ABI-defined -- see Context::CreateMemberPointerType), so it is not read
  // here.
  DWARFDIE pointee_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  DWARFDIE containing_die =
      die.GetAttributeValueAsReferenceDIE(DW_AT_containing_type);
  if (!pointee_die || !containing_die)
    return nullptr;
  Type *pointee = die.ResolveTypeUID(pointee_die);
  Type *containing = die.ResolveTypeUID(containing_die);
  if (!pointee || !containing)
    return nullptr;

  CompilerType member_pointer_type =
      cpp_typesystem::Builder(m_ts).CreateMemberPointerType(
          pointee->GetForwardCompilerType(),
          containing->GetForwardCompilerType());

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(member_pointer_type.GetByteSize(nullptr));

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, pointee_die.GetID(),
                         Type::eEncodingIsUID, decl, member_pointer_type,
                         Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParseTypedef(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();
  ConstString name(GetDIEQualifiedName(die));

  // Resolve the aliased type. A typedef can alias an incomplete type, so use
  // its forward-declared CompilerType.
  DWARFDIE underlying_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType underlying_type;
  if (underlying_die) {
    Type *underlying = die.ResolveTypeUID(underlying_die);
    if (!underlying)
      return nullptr;
    underlying_type = underlying->GetForwardCompilerType();
  }

  CompilerType typedef_type;
  {
    cpp_typesystem::Builder ts(m_ts);
    // A missing DW_AT_type means the aliased type is `void`.
    if (!underlying_type)
      underlying_type = ts.GetVoidType();
    typedef_type = ts.CreateTypedefType(name.GetStringRef(), underlying_type);
    SetTypeNameInfo(die, typedef_type, ts);
  }

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(typedef_type.GetByteSize(nullptr));

  Declaration decl = GetDIEDeclaration(die);
  return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                         underlying_die.GetID(), Type::eEncodingIsUID, decl,
                         typedef_type, Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParseCVQualifiedType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  // DWARF nests one qualifier per DIE (e.g. `const volatile int` is a
  // DW_TAG_const_type wrapping a DW_TAG_volatile_type wrapping `int`), so each
  // DIE carries exactly one of const/volatile.
  bool is_const = die.Tag() == DW_TAG_const_type;
  bool is_volatile = die.Tag() == DW_TAG_volatile_type;

  DWARFDIE underlying_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType underlying_type;
  if (underlying_die) {
    Type *underlying = die.ResolveTypeUID(underlying_die);
    if (!underlying)
      return nullptr;
    underlying_type = underlying->GetForwardCompilerType();
  }

  CompilerType cv_type;
  {
    cpp_typesystem::Builder ts(m_ts);
    // A missing DW_AT_type means the qualified type is `void` (e.g. the pointee
    // of a `const void *`).
    if (!underlying_type)
      underlying_type = ts.GetVoidType();
    cv_type = ts.CreateCVQualifiedType(underlying_type, is_const, is_volatile);
  }

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(cv_type.GetByteSize(nullptr));

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, underlying_die.GetID(),
                         Type::eEncodingIsUID, decl, cv_type,
                         Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParseAtomicType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  // A DW_TAG_atomic_type (`_Atomic(T)`) has the same size, layout and value as
  // its underlying type T. TypeSystemCpp doesn't model an atomic qualifier, so
  // treat it transparently as T -- this is enough for value inspection (e.g. a
  // libc++ `std::atomic`'s `__a_value` member is `_Atomic(int)`).
  DWARFDIE underlying_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType underlying_type;
  if (underlying_die) {
    Type *underlying = die.ResolveTypeUID(underlying_die);
    if (!underlying)
      return nullptr;
    underlying_type = underlying->GetForwardCompilerType();
  }
  if (!underlying_type) {
    cpp_typesystem::Builder ts(m_ts);
    underlying_type = ts.GetVoidType();
  }

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(underlying_type.GetByteSize(nullptr));

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, underlying_die.GetID(),
                         Type::eEncodingIsUID, decl, underlying_type,
                         Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParsePtrAuthType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  // A DW_TAG_LLVM_ptrauth_type wraps a signed pointer (or a typedef of one) and
  // carries the pointer-authentication signing schema.
  DWARFDIE underlying_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  if (!underlying_die)
    return nullptr;
  Type *underlying = die.ResolveTypeUID(underlying_die);
  if (!underlying)
    return nullptr;
  CompilerType underlying_type = underlying->GetForwardCompilerType();

  const unsigned key =
      die.GetAttributeValueAsUnsigned(DW_AT_LLVM_ptrauth_key, 0);
  const bool addr_disc = die.GetAttributeValueAsUnsigned(
                             DW_AT_LLVM_ptrauth_address_discriminated, 0) != 0;
  const unsigned extra = die.GetAttributeValueAsUnsigned(
      DW_AT_LLVM_ptrauth_extra_discriminator, 0);

  CompilerType ptrauth_type;
  {
    cpp_typesystem::Builder ts(m_ts);
    ptrauth_type =
        ts.CreatePtrAuthType(underlying_type, key, addr_disc, extra);
  }

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(ptrauth_type.GetByteSize(nullptr));

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, underlying_die.GetID(),
                         Type::eEncodingIsUID, decl, ptrauth_type,
                         Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParseEnum(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();
  ConstString name(GetDIEQualifiedName(die));
  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);
  bool is_scoped = die.GetAttributeValueAsUnsigned(DW_AT_enum_class, 0) != 0;

  // The enum's underlying integer type, if the producer recorded one.
  DWARFDIE underlying_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType underlying_type;
  if (underlying_die) {
    if (Type *underlying = die.ResolveTypeUID(underlying_die))
      underlying_type = underlying->GetForwardCompilerType();
  }

  // An opaque/forward-declared enum carries neither a DW_AT_byte_size nor an
  // underlying type. C enums default to `int`, so fall back to that (mirroring
  // TypeSystemClang) -- otherwise a pointer to such an enum has an unknown-size
  // pointee and can't be dereferenced (e.g. to report "<parent is NULL>").
  if (!byte_size) {
    if (underlying_type)
      byte_size = llvm::expectedToOptional(underlying_type.GetByteSize(nullptr));
    if (!byte_size)
      byte_size = 4;
  }

  CompilerType enum_type;
  {
    cpp_typesystem::Builder ts(m_ts);
    enum_type = ts.CreateEnumType(name.GetStringRef(), byte_size, underlying_type, is_scoped);
    cpp_typesystem::Type *cpp_type =
        TypeSystemCpp::GetCppType(enum_type.GetOpaqueQualType());
    auto &enum_node = *llvm::cast<cpp_typesystem::EnumType>(cpp_type);

    // Enumerators are few and cheap, so parse them eagerly here rather than
    // deferring to lazy completion.
    for (DWARFDIE child : die.children()) {
      if (child.Tag() != DW_TAG_enumerator)
        continue;
      const char *enumerator_name = child.GetName();
      if (!enumerator_name || !enumerator_name[0])
        continue;
      // The raw 64-bit value carries the two's-complement bits for signed
      // enumerators, which is what the value formatter compares against.
      uint64_t value = child.GetAttributeValueAsUnsigned(DW_AT_const_value, 0);
      ts.AddEnumerator(enum_node, ts.GetIdentifier(enumerator_name), value);
    }
    SetTypeNameInfo(die, enum_type, ts);
  }

  Declaration decl = GetDIEDeclaration(die);
  return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                         underlying_die.GetID(), Type::eEncodingIsUID, decl,
                         enum_type, Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParseFunctionType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  // Resolve the return type (a missing DW_AT_type means `void`).
  DWARFDIE return_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType return_type;
  if (return_die)
    if (Type *ret = die.ResolveTypeUID(return_die))
      return_type = ret->GetForwardCompilerType();

  // Resolve the parameter types, skipping the implicit object (`this`)
  // parameter (DW_AT_artificial) so a method's signature matches its
  // clang::CXXMethodDecl.
  std::vector<CompilerType> params;
  bool is_variadic = false;
  for (DWARFDIE child : die.children()) {
    if (child.Tag() == DW_TAG_formal_parameter) {
      if (child.GetAttributeValueAsUnsigned(DW_AT_artificial, 0))
        continue;
      DWARFDIE param_die = child.GetAttributeValueAsReferenceDIE(DW_AT_type);
      if (param_die)
        if (Type *p = die.ResolveTypeUID(param_die))
          params.push_back(p->GetForwardCompilerType());
    } else if (child.Tag() == DW_TAG_unspecified_parameters) {
      is_variadic = true;
    }
  }

  // An empty, non-variadic parameter list should be spelled `(void)` rather
  // than `()` when the function is explicitly prototyped (DW_AT_prototyped)
  // and the owning compile unit's language is C, not C++: in C++ `int foo();`
  // already means an empty prototype, but in C it means "unspecified
  // parameters" (K&R), so `(void)` is needed to say "takes no arguments".
  // Mirrors clang's PrintingPolicy::UseVoidForZeroParams (true iff !CPlusPlus),
  // just decided per-DIE here since TypeSystemCpp has one Context shared across
  // languages instead of TypeSystemClang's one ASTContext per language.
  // DW_AT_prototyped is looked up with check_elaborating_dies=true (bypassing
  // the DWARFBaseDIE convenience wrapper, which hardcodes it to false) because
  // it commonly lives on an out-of-line DW_AT_specification DIE rather than the
  // DW_TAG_subprogram actually being parsed here (see
  // DWARFASTParserClang::GetObjectParameter for the same pattern).
  DWARFFormValue prototyped_form_value;
  bool is_prototyped = die.GetDIE()->GetAttributeValue(
      die.GetCU(), DW_AT_prototyped, prototyped_form_value,
      /*end_attr_offset_ptr=*/nullptr, /*check_elaborating_dies=*/true);
  bool use_void_for_empty_params =
      !is_variadic && params.empty() && is_prototyped &&
      !Language::LanguageIsCPlusPlus(SymbolFileDWARF::GetLanguage(*die.GetCU()));

  CompilerType function_type;
  {
    cpp_typesystem::Builder builder(m_ts);
    if (!return_type)
      return_type = builder.GetVoidType();
    function_type = builder.CreateFunctionType(return_type, is_variadic,
                                               use_void_for_empty_params);
    for (const CompilerType &param : params)
      builder.AddParameter(function_type, param);
  }

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, /*byte_size=*/std::nullopt,
                         /*context=*/nullptr, LLDB_INVALID_UID,
                         Type::eEncodingIsUID, decl, function_type,
                         Type::ResolveState::Full);
}

TypeSP DWARFASTParserCpp::ParseTypeFromDWARF(const SymbolContext &sc,
                                             const DWARFDIE &die,
                                             bool *type_is_new_ptr) {
  if (type_is_new_ptr)
    *type_is_new_ptr = false;

  if (!die)
    return nullptr;

  SymbolFileDWARF *dwarf = die.GetDWARF();

  // Detect and break cycles, and reuse already-parsed types.
  if (auto [it, inserted] =
          dwarf->GetDIEToType().try_emplace(die.GetDIE(), DIE_IS_BEING_PARSED);
      !inserted) {
    if (it->getSecond() == nullptr || it->getSecond() == DIE_IS_BEING_PARSED)
      return nullptr;
    return it->getSecond()->shared_from_this();
  }

  TypeSP type_sp;
  switch (die.Tag()) {
  case DW_TAG_base_type:
    type_sp = ParseBaseType(die);
    break;
  case DW_TAG_structure_type:
  case DW_TAG_class_type:
  case DW_TAG_union_type:
    type_sp = ParseStructureType(die);
    break;
  case DW_TAG_array_type:
    type_sp = ParseArrayType(die);
    break;
  case DW_TAG_pointer_type:
    type_sp = ParsePointerType(die);
    break;
  case DW_TAG_reference_type:
  case DW_TAG_rvalue_reference_type:
    type_sp = ParseReferenceType(die);
    break;
  case DW_TAG_ptr_to_member_type:
    type_sp = ParsePointerToMemberType(die);
    break;
  case DW_TAG_typedef:
  case DW_TAG_template_alias:
    type_sp = ParseTypedef(die);
    break;
  case DW_TAG_const_type:
  case DW_TAG_volatile_type:
    type_sp = ParseCVQualifiedType(die);
    break;
  case DW_TAG_LLVM_ptrauth_type:
    type_sp = ParsePtrAuthType(die);
    break;
  case DW_TAG_atomic_type:
    type_sp = ParseAtomicType(die);
    break;
  case DW_TAG_enumeration_type:
    type_sp = ParseEnum(die);
    break;
  case DW_TAG_subroutine_type:
  case DW_TAG_subprogram:
    type_sp = ParseFunctionType(die);
    break;
  case DW_TAG_unspecified_type:
    type_sp = ParseUnspecifiedType(die);
    break;
  default:
    break;
  }

  if (type_sp) {
    if (type_is_new_ptr)
      *type_is_new_ptr = true;
    dwarf->GetDIEToType()[die.GetDIE()] = type_sp.get();
  }
  return type_sp;
}

cpp_typesystem::Type *
DWARFASTParserCpp::ResolveReferencedType(const DWARFDIE &referencing_die,
                                         const DWARFDIE &type_die) {
  if (!type_die)
    return nullptr;

  // Serialize the whole resolution under the type-system lock. It drives both
  // the DWARF type bookkeeping (SymbolFileDWARF's DIE->type map) and the
  // TypeSystemCpp mutation, and may run on a worker thread. The lock is
  // recursive, so nested parsing (e.g. of an array element, or of this type's
  // own referenced types) re-locks safely on the same thread.
  cpp_typesystem::Builder ts(m_ts);
  Type *resolved = referencing_die.ResolveTypeUID(type_die);
  if (!resolved)
    return nullptr;
  CompilerType forward = resolved->GetForwardCompilerType();
  return TypeSystemCpp::GetCppType(forward.GetOpaqueQualType());
}

/// A virtual base's DW_AT_data_member_location under the Itanium ABI is a
/// location expression of the form
///   DW_OP_dup DW_OP_deref DW_OP_constu N DW_OP_minus DW_OP_deref DW_OP_plus
/// which computes `obj + *(int*)(*(void**)obj - N)`: read the object's vtable
/// pointer, step back N bytes to the slot holding this base's offset, and add
/// that offset to the object address. Recognize exactly that shape and return
/// N, so a live object can be walked to the virtual base at value-inspection
/// time (see TypeSystemCpp::GetChildCompilerTypeAtIndex). Returns nullopt for
/// any other expression (e.g. a plain constant, or a non-Itanium form), in
/// which case the caller falls back to the constant byte offset.
static std::optional<uint64_t>
ExtractVBaseOffsetOffset(const DWARFDIE &die,
                         const DWARFFormValue &form_value) {
  const uint8_t *block = form_value.BlockData();
  if (!block)
    return std::nullopt;
  const uint64_t block_length = form_value.Unsigned();
  const DWARFDataExtractor &debug_info_data = die.GetData();
  lldb::offset_t block_offset = block - debug_info_data.GetDataStart();
  lldb::offset_t end = block_offset + block_length;

  auto next_op = [&](uint8_t expected) {
    return block_offset < end &&
           debug_info_data.GetU8(&block_offset) == expected;
  };

  if (!next_op(DW_OP_dup) || !next_op(DW_OP_deref))
    return std::nullopt;
  if (block_offset >= end ||
      debug_info_data.GetU8(&block_offset) != DW_OP_constu)
    return std::nullopt;
  uint64_t n = debug_info_data.GetULEB128(&block_offset);
  if (!next_op(DW_OP_minus) || !next_op(DW_OP_deref) || !next_op(DW_OP_plus))
    return std::nullopt;
  // The expression must end here (no trailing opcodes) to match exactly.
  if (block_offset != end)
    return std::nullopt;
  return n;
}

bool DWARFASTParserCpp::CompleteTypeFromDWARF(
    const DWARFDIE &die, Type *type, const CompilerType &compiler_type) {
  if (!die)
    return false;

  // A DW_TAG_structure/class/union DIE is always backed by a RecordType.
  cpp_typesystem::Type *cpp_type =
      TypeSystemCpp::GetCppType(compiler_type.GetOpaqueQualType());
  if (!cpp_type || !cpp_type->IsAggregate())
    return false;
  auto *record = static_cast<cpp_typesystem::RecordType *>(cpp_type);

  // SymbolFileDWARF::CompleteType falls back to the declaration DIE when it
  // can't find a defining DIE anywhere in the debug info (e.g. a type that is
  // only ever forward declared, like `struct FwdClass;`). Such a DIE still
  // carries DW_AT_declaration and has no members to add, so leave the record
  // incomplete instead of marking a declaration-only record complete. This is
  // what lets SBType::IsTypeComplete correctly report a forward-declared type
  // (or the pointee of a pointer to one) as incomplete.
  if (die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0))
    return false;

  // Remember the DIE that defines this record so that its member functions can
  // be parsed on demand later. Unlike fields and base classes, member functions
  // are not parsed here (they are only needed to call methods from an
  // expression); see CompleteMemberFunctionsFromDWARF.
  m_record_defining_die.insert_or_assign(record, die);

  // Mark complete up front (under the lock) so that a member whose type
  // (indirectly) refers back to this record doesn't recurse forever, and so
  // that a concurrent completion of the same record bails out here.
  {
    cpp_typesystem::Builder ts(m_ts);
    if (record->IsComplete())
      return true;
    // The record may have been created from a forward-declaration DIE (which
    // carries no DW_AT_byte_size); the definition DIE we are completing against
    // has the real size, so record it now. Without this a record defined in a
    // different compile unit than its forward declaration keeps a null byte
    // size after completion, which breaks dereferencing pointers to it and the
    // expression-parser record layout.
    if (!record->GetByteSize())
      if (std::optional<uint64_t> byte_size =
              die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size))
        record->SetByteSize(byte_size);
    // An explicit `DW_AT_alignment` (e.g. from `alignas(...)`) cannot be
    // recovered from the size, so record it for GetTypeBitAlign.
    if (std::optional<uint64_t> align =
            die.GetAttributeValueAsOptionalUnsigned(DW_AT_alignment))
      record->SetAlignInBits(*align * 8);
    ts.SetRecordComplete(*record);

    // DW_AT_calling_convention on the record describes how it is passed and
    // returned by value. Record it so ClangASTGenerator can reproduce the ABI
    // when the expression evaluator calls a function that takes/returns this
    // record by value (e.g. a struct with a non-trivial copy constructor is
    // passed by reference / returned via sret). Mirrors DWARFASTParserClang.
    switch (die.GetAttributeValueAsUnsigned(
        DW_AT_calling_convention, llvm::dwarf::DW_CC_normal)) {
    case llvm::dwarf::DW_CC_pass_by_value:
      ts.SetRecordArgPassingKind(
          *record, cpp_typesystem::RecordType::ArgPassingKind::PassByValue);
      break;
    case llvm::dwarf::DW_CC_pass_by_reference:
      ts.SetRecordArgPassingKind(
          *record,
          cpp_typesystem::RecordType::ArgPassingKind::CannotPassInRegs);
      break;
    default:
      break;
    }
  }

  // C++-only information (base classes) is only stored on ClassType.
  auto *cpp_class = llvm::dyn_cast<cpp_typesystem::ClassType>(record);
  // An Objective-C class stores its (single) superclass separately.
  auto *objc_interface = llvm::dyn_cast<cpp_typesystem::ObjCInterfaceType>(record);

  // Collect the base classes, members and template arguments in declaration
  // order, along with the DWARF reference to each one's type. This DWARF
  // traversal runs single threaded; only the (heavier) type resolution below
  // is spread out.
  struct MemberInfo {
    enum class Kind {
      Field,
      Base,
      TemplateType,
      TemplateValue,
      TemplateTemplate,
      NestedType,
      StaticDataMember
    } kind;
    llvm::StringRef name; // Field/nested-type/static-member name; else empty.
    uint64_t byte_offset = 0;
    uint64_t value = 0; // Value of a non-type template argument.
    DWARFDIE referencing_die;
    DWARFDIE type_die;
    uint32_t bit_size = 0; // DW_AT_bit_size of a bitfield member (0 if none).
    // DW_AT_data_bit_offset of a bitfield member, or UINT64_MAX if absent.
    uint64_t data_bit_offset = UINT64_MAX;
    // For a template argument: true if it was defaulted (DW_AT_default_value).
    bool is_default = false;
    // For a base class: true if it is a virtual base (DW_AT_virtuality).
    bool is_virtual = false;
    // For a virtual base: the vtable-pointer-relative byte offset extracted from
    // its DW_AT_data_member_location expression (see BaseClass), if recognized.
    std::optional<uint64_t> vbase_offset_offset;
    // For a static data member: its DW_AT_const_value, if present.
    std::optional<uint64_t> const_value;
  };
  std::vector<MemberInfo> members;

  // If the record starts with an artificial vtable pointer (which we skip as a
  // field below, since Clang re-creates it), this holds the bit offset just
  // past it. The unnamed-bitfield gap detection uses it to avoid synthesizing a
  // spurious padding field between the vtable pointer and the first bitfield
  // (mirroring DWARFASTParserClang, where the vptr advances last_field_end).
  uint64_t vtable_ptr_end_bits = 0;

  // True if this record is a polymorphic C++ class: it has an artificial
  // `_vptr$` member (a direct vtable pointer) or a virtual base class. Recorded
  // eagerly here (during completion) so C++ dynamic-type detection does not have
  // to force lazy member-function parsing (see ClassType::IsPolymorphic).
  bool has_vtable = false;

  // True once we see any template-parameter DIE (type/value parameter or a
  // GNU parameter pack), i.e. this record is a class-template instantiation.
  // An instantiation over an *empty* pack (e.g. `TypePack<>`) emits an empty
  // DW_TAG_GNU_template_parameter_pack and no argument DIEs, so it has zero
  // modeled arguments yet must still be recognised as a template so it prints
  // an (empty) `<>` argument list.
  bool is_template = false;

  // Add the template argument described by a DW_TAG_template_{type,value}_-
  // parameter DIE. Shared so it can also be applied to the parameters nested
  // inside a DW_TAG_GNU_template_parameter_pack (a variadic `Ss...`), which the
  // compiler emits as a wrapper DIE around one child per expanded argument.
  auto add_template_param = [&members](DWARFDIE param) {
    if (param.Tag() == DW_TAG_template_type_parameter) {
      members.push_back({MemberInfo::Kind::TemplateType, param.GetName(),
                         /*byte_offset=*/0,
                         /*value=*/0, param,
                         param.GetAttributeValueAsReferenceDIE(DW_AT_type)});
    } else if (param.Tag() == DW_TAG_GNU_template_template_param) {
      // A template-template argument (e.g. the `T1` in `C<float, T1>`). It is
      // not a modeled type; only its spelling (DW_AT_GNU_template_name) is kept
      // so the instantiation's name can be reconstructed.
      const char *template_name =
          param.GetAttributeValueAsString(DW_AT_GNU_template_name, nullptr);
      members.push_back({MemberInfo::Kind::TemplateTemplate,
                         llvm::StringRef(template_name ? template_name : ""),
                         /*byte_offset=*/0, /*value=*/0, param, DWARFDIE()});
    } else {
      members.push_back(
          {MemberInfo::Kind::TemplateValue, param.GetName(), /*byte_offset=*/0,
           param.GetAttributeValueAsUnsigned(DW_AT_const_value, 0), param,
           param.GetAttributeValueAsReferenceDIE(DW_AT_type)});
    }
    members.back().is_default =
        param.GetAttributeValueAsUnsigned(DW_AT_default_value, 0) != 0;
  };

  for (DWARFDIE child : die.children()) {
    const dw_tag_t tag = child.Tag();
    if (tag == DW_TAG_inheritance) {
      // Base classes exist on C++ class types; an ObjC interface records its
      // single superclass the same way (integrated via SetObjCSuperClass).
      if (!cpp_class && !objc_interface)
        continue;
      // A virtual base's DW_AT_data_member_location is a location expression
      // (evaluated against a live object), not a constant, so the byte offset
      // read below is meaningless for it; mark it virtual and let the record
      // layout supply the real offset (mirroring DWARFASTParserClang, which
      // likewise omits a constant offset for virtual bases).
      const bool base_is_virtual =
          child.GetAttributeValueAsUnsigned(DW_AT_virtuality, 0) != 0;
      // For a virtual base, recover the vtable-pointer-relative offset from its
      // location expression so a live object can be walked to it later.
      std::optional<uint64_t> vbase_offset_offset;
      if (base_is_virtual) {
        if (std::optional<DWARFFormValue> loc =
                child.find(DW_AT_data_member_location))
          vbase_offset_offset = ExtractVBaseOffsetOffset(child, *loc);
      }
      members.push_back(
          {MemberInfo::Kind::Base, /*name=*/{},
           child.GetAttributeValueAsUnsigned(DW_AT_data_member_location, 0),
           /*value=*/0, child,
           child.GetAttributeValueAsReferenceDIE(DW_AT_type)});
      members.back().is_virtual = base_is_virtual;
      members.back().vbase_offset_offset = vbase_offset_offset;
      // A virtual base is reached through the vtable (its offset lives in the
      // vtable), so the class is polymorphic.
      if (base_is_virtual)
        has_vtable = true;
    } else if (tag == DW_TAG_member) {
      // Skip the artificial vtable pointer (`_vptr$Class`): Clang re-creates it
      // itself for a polymorphic class, so adding it as a field here would
      // duplicate it and overlap at offset 0 in the record layout.
      if (child.GetAttributeValueAsUnsigned(DW_AT_artificial, 0)) {
        const char *member_name = child.GetName();
        llvm::StringRef member_name_ref(member_name ? member_name : "");
        if (member_name_ref.starts_with("_vptr$") ||
            member_name_ref.starts_with("_vptr.")) {
          // Record where the vtable pointer ends so gap detection below does
          // not treat the space it occupies as an unnamed bitfield.
          uint64_t vptr_offset = child.GetAttributeValueAsUnsigned(
              DW_AT_data_member_location, 0);
          vtable_ptr_end_bits =
              vptr_offset * 8 + child.GetCU()->GetAddressByteSize() * 8;
          has_vtable = true;
          continue;
        }
      }
      std::optional<uint64_t> data_member_location =
          child.GetAttributeValueAsOptionalUnsigned(DW_AT_data_member_location);
      std::optional<uint64_t> data_bit_offset =
          child.GetAttributeValueAsOptionalUnsigned(DW_AT_data_bit_offset);
      // A static data member occupies no storage in the object, so it has
      // neither a DW_AT_data_member_location nor a DW_AT_data_bit_offset and
      // (pre-DWARFv5) is marked DW_AT_declaration. Model it as a static data
      // member rather than a field (a field would get a bogus offset of 0 and
      // overlap the real members). DWARFv5 emits static data members as
      // DW_TAG_variable, handled below.
      if (!data_member_location && !data_bit_offset &&
          child.GetAttributeValueAsUnsigned(DW_AT_declaration, 0)) {
        const char *member_name = child.GetName();
        if (member_name && member_name[0]) {
          MemberInfo info{MemberInfo::Kind::StaticDataMember, member_name,
                          /*byte_offset=*/0, /*value=*/0, child,
                          child.GetAttributeValueAsReferenceDIE(DW_AT_type)};
          info.const_value =
              child.GetAttributeValueAsOptionalUnsigned(DW_AT_const_value);
          members.push_back(info);
        }
        continue;
      }
      members.push_back(
          {MemberInfo::Kind::Field, child.GetName(),
           data_member_location.value_or(0),
           /*value=*/0, child,
           child.GetAttributeValueAsReferenceDIE(DW_AT_type),
           /*bit_size=*/
           static_cast<uint32_t>(
               child.GetAttributeValueAsUnsigned(DW_AT_bit_size, 0)),
           data_bit_offset.value_or(UINT64_MAX)});
    } else if (tag == DW_TAG_template_type_parameter ||
               tag == DW_TAG_template_value_parameter ||
               tag == DW_TAG_GNU_template_template_param) {
      is_template = true;
      add_template_param(child);
    } else if (tag == DW_TAG_GNU_template_parameter_pack) {
      // A variadic template parameter pack (`Ss...`): each expanded argument is
      // a child DW_TAG_template_{type,value}_parameter (or a template-template
      // parameter). Add them individually so they appear in the reconstructed
      // name (e.g. "FooPack<char, int>" or "C<float, T1>"). The pack itself
      // (even when empty) marks this as a template instantiation.
      is_template = true;
      for (DWARFDIE pack_param : child.children())
        if (pack_param.Tag() == DW_TAG_template_type_parameter ||
            pack_param.Tag() == DW_TAG_template_value_parameter ||
            pack_param.Tag() == DW_TAG_GNU_template_template_param)
          add_template_param(pack_param);
    } else if (tag == DW_TAG_typedef || tag == DW_TAG_template_alias ||
               tag == DW_TAG_structure_type || tag == DW_TAG_class_type ||
               tag == DW_TAG_union_type || tag == DW_TAG_enumeration_type) {
      // A type declared inside this record. Resolving the child DIE itself
      // yields the nested type; data formatters look these up by name (e.g. a
      // container's "__node_pointer"). Skip anonymous ones.
      const char *nested_name = child.GetName();
      if (nested_name && nested_name[0])
        members.push_back({MemberInfo::Kind::NestedType, nested_name,
                           /*byte_offset=*/0, /*value=*/0, die, child});
    } else if (tag == DW_TAG_variable) {
      // A DW_TAG_variable child of a record is a static data member (DWARFv5;
      // pre-DWARFv5 they appear as declaration-only DW_TAG_member, handled
      // above). It occupies no storage in the object.
      const char *member_name = child.GetName();
      if (member_name && member_name[0]) {
        MemberInfo info{MemberInfo::Kind::StaticDataMember, member_name,
                        /*byte_offset=*/0, /*value=*/0, child,
                        child.GetAttributeValueAsReferenceDIE(DW_AT_type)};
        info.const_value =
            child.GetAttributeValueAsOptionalUnsigned(DW_AT_const_value);
        members.push_back(info);
      }
    }
  }

  // Resolve each referenced type, optionally on a worker thread. Each future
  // serializes its type-system access through the lock (see
  // ResolveReferencedType), so this stays thread-safe.
  std::vector<std::future<cpp_typesystem::Type *>> resolved;
  resolved.reserve(members.size());
  for (const MemberInfo &member : members) {
    DWARFDIE referencing_die = member.referencing_die;
    DWARFDIE type_die = member.type_die;
    resolved.push_back(
        std::async(kMemberResolutionPolicy, [this, referencing_die, type_die] {
          return ResolveReferencedType(referencing_die, type_die);
        }));
  }

  // Wait for the workers *without* holding the lock -- they acquire it
  // themselves, so holding it here would deadlock.
  std::vector<cpp_typesystem::Type *> member_types(members.size());
  for (size_t i = 0; i < members.size(); ++i)
    member_types[i] = resolved[i].get();

  // Integrate the resolved base classes, members and template arguments into
  // the record, in declaration order, under a single lock.
  cpp_typesystem::Builder ts(m_ts);

  // Record whether this is a class-template instantiation (see `is_template`).
  if (is_template)
    ts.SetRecordTemplateInstantiation(*record);

  // Compilers omit unnamed bitfields (padding) from DWARF, but LLDB
  // reconstructs them so the gaps between named bitfields are visible when
  // inspecting a value. We track where the previous field ended (in bits) and,
  // whenever a bitfield starts past that point, synthesize a padding member for
  // the hole. This mirrors DWARFASTParserClang's unnamed-bitfield handling.
  bool have_base = false;
  for (const MemberInfo &member : members)
    if (member.kind == MemberInfo::Kind::Base) {
      have_base = true;
      break;
    }
  // The synthetic padding member is an `int` (matching TypeSystemClang, which
  // uses a signed word-width builtin). Fetch it once.
  constexpr uint64_t word_width = 32;
  auto *padding_type = static_cast<cpp_typesystem::Type *>(
      ts.GetBuiltinType("int", word_width / 8, lldb::eEncodingSint,
                        lldb::eFormatDecimal)
          .GetOpaqueQualType());
  // State of the previous field, in absolute bits from the start of the record.
  // If a vtable pointer was skipped above, treat it as the previous (non-
  // bitfield) field so the space it occupies is not mistaken for a gap that
  // needs a synthetic unnamed bitfield.
  uint64_t last_field_end = vtable_ptr_end_bits;
  bool last_field_is_bitfield = false;
  bool seen_field = false;

  for (size_t i = 0; i < members.size(); ++i) {
    cpp_typesystem::Type *member_type = member_types[i];
    const MemberInfo &member = members[i];
    switch (member.kind) {
    case MemberInfo::Kind::Base:
      if (member_type) {
        if (objc_interface)
          ts.SetObjCSuperClass(*objc_interface, member_type);
        else if (cpp_class)
          ts.AddBaseClass(*cpp_class, member_type, member.byte_offset,
                          member.is_virtual, member.vbase_offset_offset);
      }
      break;
    case MemberInfo::Kind::Field:
      if (member_type) {
        // For a bitfield, translate the DWARF bit position into the storage
        // unit's byte offset plus the bit offset within it (mirroring
        // TypeSystemClang and what GetChildCompilerTypeAtIndex expects).
        uint64_t byte_offset = member.byte_offset;
        uint32_t bitfield_bit_size = 0;
        uint32_t bitfield_bit_offset = 0;
        // Absolute bit offset/end of this field within the record, used to
        // detect unnamed-bitfield gaps.
        uint64_t abs_bit_offset;
        uint64_t this_bit_size;
        const bool is_bitfield = member.bit_size > 0;
        if (is_bitfield) {
          bitfield_bit_size = member.bit_size;
          abs_bit_offset = member.data_bit_offset != UINT64_MAX
                               ? member.data_bit_offset
                               : member.byte_offset * 8;
          this_bit_size = member.bit_size;
          uint64_t storage_bits = member_type->GetByteSize().value_or(0) * 8;
          if (storage_bits) {
            bitfield_bit_offset = abs_bit_offset % storage_bits;
            byte_offset = (abs_bit_offset - bitfield_bit_offset) / 8;
          } else {
            byte_offset = abs_bit_offset / 8;
          }
        } else {
          abs_bit_offset = member.byte_offset * 8;
          this_bit_size = member_type->GetByteSize().value_or(0) * 8;
        }

        // Fill any gap before a bitfield with a synthetic unnamed bitfield.
        if (is_bitfield && padding_type) {
          uint64_t gap_start = last_field_end;
          // If the previous field was not a bitfield and did not end on a word
          // boundary, its padding fills the rest of the word, so the gap does
          // not start until the next word.
          if (!last_field_is_bitfield && gap_start != 0 &&
              (gap_start % word_width) != 0)
            gap_start += word_width - (gap_start % word_width);

          const bool this_is_first_field = !seen_field;
          if (abs_bit_offset > gap_start &&
              !(have_base && this_is_first_field)) {
            uint64_t pad_bits = abs_bit_offset - gap_start;
            uint32_t pad_bit_in_word =
                static_cast<uint32_t>(gap_start % word_width);
            uint64_t pad_byte_offset = (gap_start - pad_bit_in_word) / 8;
            ts.AddField(*record, ts.GetIdentifier(llvm::StringRef()),
                        padding_type, pad_byte_offset,
                        static_cast<uint32_t>(pad_bits), pad_bit_in_word);
          }
        }

        // Intern the member name through the type system's Context so its
        // storage is owned alongside the types (rather than pointing into
        // DWARF data).
        ts.AddField(*record, ts.GetIdentifier(member.name), member_type,
                    byte_offset, bitfield_bit_size, bitfield_bit_offset);

        // An unnamed data member (no DW_AT_name, and not an unnamed bitfield)
        // whose type is an unnamed record is an anonymous struct/union: its
        // members are injected into this record's scope. Mark the member's
        // record type so SBType::IsAnonymousType reports it (mirroring clang's
        // RecordDecl::isAnonymousStructOrUnion). A merely unnamed record given a
        // member name (`struct { int x; } m;`) is not anonymous. Also record
        // `record` (the enclosing MySock-like type) as its anonymous parent, so
        // TypeSystemCpp::BuildDisplayName/GetTypeName can qualify the otherwise
        // nameless "(unnamed union)" spelling as "MySock::(anonymous union)",
        // matching clang's NamedDecl::printNestedNameSpecifier.
        if (!is_bitfield && member.name.empty())
          if (auto *field_record =
                  llvm::dyn_cast<cpp_typesystem::RecordType>(member_type))
            if (field_record->GetName().GetName().empty())
              ts.SetRecordAnonymousStructOrUnion(*field_record, *record);
        // Advance the running field-end used for gap detection.
        uint64_t this_end = abs_bit_offset + this_bit_size;
        if (this_end > last_field_end || !seen_field)
          last_field_end = this_end;
        last_field_is_bitfield = is_bitfield;
        seen_field = true;
      }
      break;
    case MemberInfo::Kind::TemplateType:
      ts.AddTemplateArgument(*record, lldb::eTemplateArgumentKindType,
                             member_type, /*integral_value=*/0,
                             member.is_default);
      break;
    case MemberInfo::Kind::TemplateValue:
      ts.AddTemplateArgument(*record, lldb::eTemplateArgumentKindIntegral,
                             member_type, member.value, member.is_default);
      break;
    case MemberInfo::Kind::TemplateTemplate:
      ts.AddTemplateTemplateArgument(*record, member.name, member.is_default);
      break;
    case MemberInfo::Kind::NestedType:
      if (member_type)
        ts.AddNestedType(*record, ts.GetIdentifier(member.name), member_type);
      break;
    case MemberInfo::Kind::StaticDataMember:
      if (member_type) {
        // The linkage name used to resolve the member's runtime address. The
        // declaration DIE usually carries none (the definition, a separate
        // CU-scope DW_TAG_variable, does), in which case clang's mangler
        // reconstructs it from the record's qualified name when building the
        // VarDecl. Pass through any DW_AT_linkage_name the declaration does
        // have.
        const char *mangled =
            member.referencing_die.GetMangledName(
                /*substitute_name_allowed=*/false);
        ts.AddStaticDataMember(*record, member.name, member_type,
                               mangled ? mangled : "",
                               member.const_value);
      }
      break;
    }
  }

  // Mark the class polymorphic if it has its own vtable pointer / a virtual
  // base, or if it inherits a vtable from a (direct) polymorphic base. Recorded
  // eagerly so C++ dynamic-type detection (IsPossibleDynamicType) works without
  // forcing lazy member-function parsing (see ClassType::IsPolymorphic).
  if (cpp_class) {
    if (!has_vtable) {
      for (size_t i = 0; i < members.size(); ++i) {
        if (members[i].kind != MemberInfo::Kind::Base)
          continue;
        if (auto *base = member_types[i];
            base && base->IsPolymorphic()) {
          has_vtable = true;
          break;
        }
      }
    }
    if (has_vtable)
      ts.SetRecordPolymorphic(*cpp_class);
  }

  return true;
}

void DWARFASTParserCpp::CompleteMemberFunctionsFromDWARF(
    cpp_typesystem::RecordType &record) {
  // Serialize under the type-system lock, matching CompleteTypeFromDWARF. Parse
  // the member functions at most once (parsing again would append duplicates),
  // so mark them parsed up front.
  cpp_typesystem::Builder ts(m_ts);
  if (record.AreMemberFunctionsParsed())
    return;
  ts.SetRecordMemberFunctionsParsed(record);

  // The defining DIE was recorded when the record was completed. If the record
  // wasn't completed from DWARF (e.g. an expression-synthesized record) there
  // is nothing to parse.
  auto die_it = m_record_defining_die.find(&record);
  if (die_it == m_record_defining_die.end())
    return;
  DWARFDIE die = die_it->second;

  // An Objective-C interface's "member functions" are its methods, which are
  // messaged (not called directly). They live in DWARF both as child
  // DW_TAG_subprogram declarations of the class DIE and as standalone
  // definitions (named `+[Class sel:]` / `-[Class sel:]`) reachable through the
  // ObjC-method index. Parse both so the expression parser can type-check and
  // lower a message send.
  if (auto *objc_iface =
          llvm::dyn_cast<cpp_typesystem::ObjCInterfaceType>(&record)) {
    CompleteObjCMethodsFromDWARF(*objc_iface, die, ts);
    return;
  }

  // The record's unqualified base name (no template arguments), used to detect
  // constructors: a member subprogram whose unqualified name equals the class
  // name (and which has no return type) is a constructor. A destructor's name
  // begins with `~`. This mirrors how DWARFASTParserClang / TypeSystemClang
  // decide is_constructor / is_destructor.
  llvm::StringRef record_base_name = record.GetUnqualifiedName().GetName();
  if (record_base_name.empty())
    record_base_name = record.GetName().GetName();
  record_base_name = record_base_name.take_until([](char c) { return c == '<'; });

  // Member functions: parse the class's DW_TAG_subprogram children so calls
  // like `obj.method()` / `this->method()` can be resolved and JIT-called.
  for (DWARFDIE child : die.children()) {
    if (child.Tag() != DW_TAG_subprogram)
      continue;
    const char *method_name = child.GetName();
    if (!method_name || !method_name[0])
      continue;
    // Skip compiler-generated special members (implicit ctors/dtors/etc.).
    if (child.GetAttributeValueAsUnsigned(DW_AT_artificial, 0))
      continue;

    Type *func_type = die.ResolveTypeUID(child);
    if (!func_type)
      continue;

    bool is_virtual =
        child.GetAttributeValueAsUnsigned(DW_AT_virtuality, 0) != 0;
    // Ref-qualifier (`&`/`&&`): DWARF encodes the lvalue one via DW_AT_reference
    // and the rvalue one via DW_AT_rvalue_reference. A class can overload on it,
    // so it must be carried through to the synthesized CXXMethodDecl.
    cpp_typesystem::RefQualifier ref_qualifier =
        cpp_typesystem::RefQualifier::None;
    if (child.GetAttributeValueAsUnsigned(DW_AT_rvalue_reference, 0))
      ref_qualifier = cpp_typesystem::RefQualifier::RValue;
    else if (child.GetAttributeValueAsUnsigned(DW_AT_reference, 0))
      ref_qualifier = cpp_typesystem::RefQualifier::LValue;
    // A non-static method has an artificial `this` parameter; a `const` /
    // `volatile` method has a `this` whose pointee is cv-qualified.
    bool is_static = true;
    bool is_const = false;
    bool is_volatile = false;
    for (DWARFDIE param : child.children()) {
      if (param.Tag() != DW_TAG_formal_parameter ||
          !param.GetAttributeValueAsUnsigned(DW_AT_artificial, 0))
        continue;
      is_static = false;
      if (DWARFDIE this_die = param.GetAttributeValueAsReferenceDIE(DW_AT_type))
        if (Type *this_type = die.ResolveTypeUID(this_die)) {
          CompilerType pointee =
              this_type->GetForwardCompilerType().GetPointeeType();
          // clang CVR bit layout: Const = 0x1, Volatile = 0x4.
          unsigned quals = pointee.GetTypeQualifiers();
          is_const = (quals & 0x1) != 0;
          is_volatile = (quals & 0x4) != 0;
        }
      break;
    }

    const std::string asm_label = MakeFuncAsmLabel(child);
    const char *mangled_name = child.GetMangledName(
        /*substitute_name_allowed=*/false);

    // Classify constructors/destructors so the expression evaluator can build
    // the proper C++ declaration name (`Foo(2)` must resolve to a constructor).
    // A destructor's name begins with `~`; a constructor's unqualified name
    // equals the class's base name (matching how DWARFASTParserClang /
    // TypeSystemClang compare the method name against the record decl name).
    cpp_typesystem::MemberFunctionKind kind =
        cpp_typesystem::MemberFunctionKind::Method;
    llvm::StringRef name_ref(method_name);
    if (name_ref.starts_with("~"))
      kind = cpp_typesystem::MemberFunctionKind::Destructor;
    else if (!record_base_name.empty() && name_ref == record_base_name)
      kind = cpp_typesystem::MemberFunctionKind::Constructor;


    ts.AddMemberFunction(record, method_name,
                         func_type->GetForwardCompilerType(),
                         asm_label,
                         mangled_name ? mangled_name : "",
                         is_static, is_const, is_volatile, is_virtual,
                         ref_qualifier, kind);
  }
}

void DWARFASTParserCpp::CompleteObjCMethodsFromDWARF(
    cpp_typesystem::ObjCInterfaceType &iface, const DWARFDIE &class_die,
    cpp_typesystem::Builder &ts) {
  llvm::StringRef class_name = iface.GetName().GetName();

  // Track which method DIEs we've already added so a method that appears both
  // as a child declaration of the class DIE and as a standalone definition
  // (reachable via the index) is only added once.
  llvm::DenseSet<dw_offset_t> seen;

  auto add_method = [&](const DWARFDIE &method_die) {
    const char *name = method_die.GetName();
    if (!name || !name[0])
      return;
    std::optional<const ObjCLanguage::ObjCMethodName> objc_name =
        ObjCLanguage::ObjCMethodName::Create(name, /*strict=*/true);
    if (!objc_name)
      return;
    // Only methods of this interface.
    if (objc_name->GetClassName() != class_name)
      return;
    if (!seen.insert(method_die.GetOffset()).second)
      return;

    // The FunctionType parsed from the DIE already strips the artificial
    // self/_cmd parameters (see ParseFunctionType), matching what a
    // clang::ObjCMethodDecl expects (one parameter per selector argument).
    Type *func_type = class_die.ResolveTypeUID(method_die);
    if (!func_type)
      return;

    bool is_variadic = false;
    for (DWARFDIE param : method_die.children())
      if (param.Tag() == DW_TAG_unspecified_parameters)
        is_variadic = true;

    const bool is_direct =
        method_die.GetAttributeValueAsUnsigned(DW_AT_APPLE_objc_direct, 0) !=
        0;

    const std::string asm_label = MakeFuncAsmLabel(method_die);

    ts.AddObjCMethod(iface, name,
                     func_type->GetForwardCompilerType(),
                     asm_label, objc_name->IsClassMethod(),
                     is_variadic, is_direct);
  };

  // Method declarations that are children of the class DIE.
  for (DWARFDIE child : class_die.children())
    if (child.Tag() == DW_TAG_subprogram)
      add_method(child);

  // Standalone method definitions (and declarations in other CUs) reachable
  // through the ObjC-method index (mirrors DWARFASTParserClang, which calls
  // dwarf->GetObjCMethods to find every `+[Class sel]` / `-[Class sel]` DIE).
  if (SymbolFileDWARF *dwarf = class_die.GetDWARF())
    dwarf->GetObjCMethods(ConstString(class_name),
                          [&](DWARFDIE method_die) {
                            add_method(method_die);
                            return IterationAction::Continue;
                          });
}
