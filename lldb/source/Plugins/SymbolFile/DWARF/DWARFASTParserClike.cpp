//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "DWARFASTParserClike.h"

#include "DWARFASTParser.h"
#include "DWARFDIE.h"
#include "DWARFDeclContext.h"
#include "DWARFDefines.h"
#include "DWARFUnit.h"
#include "SymbolFileDWARF.h"

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeC.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "lldb/Core/Declaration.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Core/Module.h"
#include "lldb/Expression/DWARFExpressionList.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Target/Language.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/DWARF/DWARFTypePrinter.h"

#include <algorithm>
#include <future>
#include <vector>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;
using namespace llvm::dwarf;

// Launch policy for resolving a record's referenced member/base types while
// completing it. The DWARF/SymbolFile side is not thread safe here:
// SymbolFileDWARF guards itself with the Module's recursive_mutex, which
// CompleteType already holds while CompleteTypeFromDWARF runs. A worker thread
// calling ResolveTypeUID would block trying to re-acquire that mutex
// (recursive_mutex only re-enters on the owning thread) while this thread waits
// on the worker -- a deadlock.
//
// So the resolutions run with std::launch::deferred: each future executes
// inline, on this thread, when its result is needed. Since that makes this
// single-threaded, TypeSystemClike itself does no locking (see
// clike::Builder). The work is nevertheless structured as independent
// futures, so it can be spread across cores once the DWARF reads no longer
// require the Module mutex to be held across completion -- but that will also
// mean giving TypeSystemClike its serialization back.
static constexpr std::launch kMemberResolutionPolicy = std::launch::deferred;

DWARFASTParserClike::DWARFASTParserClike(TypeSystemClike &ts)
    : DWARFASTParser(Kind::DWARFASTParserClike), m_ts(ts) {}

DWARFASTParserClike::~DWARFASTParserClike() = default;

Function *DWARFASTParserClike::ParseFunctionFromDWARF(
    CompileUnit &comp_unit, const DWARFDIE &die, AddressRanges func_ranges) {
  if (die.Tag() != DW_TAG_subprogram)
    return nullptr;

  if (!IsSupportedDWARFVersion(die))
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
/// enumerated builtin get their format from TypeSystemClike's canonical
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

TypeSP DWARFASTParserClike::ParseBaseType(const DWARFDIE &die) {
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

  // Map this base type onto the matching canonical builtin type, falling back
  // to a bespoke type (using the format derived above) if it is not one of the
  // enumerated builtins.
  CompilerType compiler_type = clike::Builder(m_ts).GetBuiltinType(
      name.GetStringRef(), byte_size, encoding, format);

  Declaration decl = GetDIEDeclaration(die);
  return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                         LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                         compiler_type, Type::ResolveState::Full);
}

TypeSP DWARFASTParserClike::ParseUnspecifiedType(const DWARFDIE &die) {
  // The only DW_TAG_unspecified_type LLDB models is `std::nullptr_t`
  // (a.k.a. `decltype(nullptr)`), which Clang emits with that name and no
  // DW_AT_type. Map it onto the canonical nullptr_t builtin.
  SymbolFileDWARF *dwarf = die.GetDWARF();
  ConstString name(die.GetName());
  llvm::StringRef n = name.GetStringRef();
  if (n != "decltype(nullptr)" && n != "std::nullptr_t" && n != "nullptr_t")
    return nullptr;

  const uint64_t ptr_size = m_ts.GetPointerByteSize();
  clike::Builder builder(m_ts);
  CompilerType compiler_type = builder.GetBuiltinType(
      "decltype(nullptr)", ptr_size, lldb::eEncodingUint, lldb::eFormatHex);
  Declaration decl = GetDIEDeclaration(die);
  return dwarf->MakeType(die.GetID(), name, ptr_size, /*context=*/nullptr,
                         LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                         compiler_type, Type::ResolveState::Full);
}

std::string DWARFASTParserClike::GetDIEClassTemplateParams(DWARFDIE die) {
  // Class templates aren't modeled yet.
  return {};
}

/// Emit the "Scope::" prefix for the DIE \p die by walking its ancestors,
/// mirroring llvm::DWARFTypePrinter::appendScopes but SKIPPING DW_TAG_module
/// scopes. In -gmodules debug info, types defined in a PCH/module are nested
/// under a DW_TAG_module scope (named e.g. "pch.h"); that scope is a debug-info
/// container, not a C++ decl context, so it must not appear in the type name
/// (otherwise "IntContainer" would print as "pch.h::IntContainer"). This
/// matches DWARFASTParserClang, which likewise ignores module scopes for
/// naming.
static void
AppendScopesSkippingModules(llvm::DWARFTypePrinter<DWARFDIE> &printer,
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
DWARFASTParserClike::ConstructDemangledNameFromDWARF(const DWARFDIE &die) {
  // TypeSystemClike must not touch clang decl contexts, so (unlike
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
        type_name =
            param_type->GetForwardCompilerType().GetTypeName().GetString();
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

/// The unqualified spelling of a named type DIE, including any reconstructed
/// template arguments (e.g. "FwdTemplateClass<int>"). This mirrors
/// GetDIEQualifiedName but omits the enclosing namespace/class scopes, since
/// TypeSystemClike stores the scope separately (see clike::DeclScope) and
/// rebuilds the qualified display name itself.
///
/// For a non-template type, or a type whose DW_AT_name already carries the
/// "<...>" template arguments, this yields the same string as the bare
/// DW_AT_name; it only differs for a template specialization whose DW_AT_name
/// is just the template's base name (e.g. a forward-declared
/// `FwdTemplateClass<int>`, whose declaration DIE is named "FwdTemplateClass"
/// with separate DW_TAG_template_*_parameter children).
static std::string GetDIEUnqualifiedName(const DWARFDIE &die) {
  // A DIE with no DW_AT_name is an unnamed type (e.g. a lambda closure or a
  // function-local unnamed struct/union/enum). llvm::DWARFTypePrinter would
  // emit a bogus bare tag keyword (e.g. "class ") for it -- mirrors the same
  // guard in GetDIEQualifiedName. Let the type system supply an
  // "(unnamed ...)" display name instead.
  if (!die.GetName())
    return std::string();

  // If the DW_AT_name already carries the template arguments, use it verbatim
  // so we don't reconstruct and duplicate them.
  const char *name = die.GetName();
  if (llvm::StringRef(name).contains('<'))
    return name;

  std::string result;
  llvm::raw_string_ostream os(result);
  llvm::DWARFTypePrinter<DWARFDIE>(os).appendUnqualifiedName(die);
  return result;
}

/// The namespace the type \p die describes is declared in and its unqualified
/// DW_AT_name spelling, so TypeSystemClike can rebuild the display name while
/// hiding inline namespaces.
static clike::DeclScope GetTypeDeclScope(const DWARFDIE &die,
                                         clike::Builder &builder) {
  clike::Identifier unqualified_name;
  if (die.GetName())
    unqualified_name = builder.GetIdentifier(GetDIEUnqualifiedName(die));
  return clike::DeclScope(/*decl_context=*/nullptr, unqualified_name);
}

CompilerDeclContext
DWARFASTParserClike::GetDeclContextForUIDFromDWARF(const DWARFDIE &die) {
  return CompilerDeclContext();
}

CompilerDeclContext
DWARFASTParserClike::GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) {
  return CompilerDeclContext();
}

TypeSP DWARFASTParserClike::ParseStructureType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  // Record the fully-qualified, template-argument-bearing spelling of the type
  // (e.g. the instantiation "Wrapper<int>" or "std::__1::vector<int, ...>")
  // rather than the bare DW_AT_name ("Wrapper" / "vector").
  ConstString name(GetDIEQualifiedName(die));
  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);

  bool is_union = die.Tag() == DW_TAG_union_type;
  // Both `struct` and `class` are modeled by the same record type, so remember
  // the source keyword to reproduce it in display names (e.g. for an unnamed
  // record: "(unnamed struct)" vs "(unnamed class)").
  bool is_class_keyword = die.Tag() == DW_TAG_class_type;

  CompilerType compiler_type;
  {
    clike::Builder builder(m_ts);
    compiler_type = builder.CreateRecordType(name.GetStringRef(), byte_size,
                                             is_union, is_class_keyword,
                                             GetTypeDeclScope(die, builder));
  }

  Declaration decl = GetDIEDeclaration(die);
  // The lldb_private::Type record (unlike the clike::Type above) is
  // named with the bare, unqualified DW_AT_name spelling -- e.g. "valarray"
  // rather than "std::__1::valarray<int>" -- to match TypeSystemClang (whose
  // Type::m_name is the raw DW_AT_name), since e.g. DIL's out-of-bounds-index
  // diagnostic prints this name and is tested against that unqualified form.
  ConstString unqualified_name(GetDIEUnqualifiedName(die));
  TypeSP type_sp = dwarf->MakeType(die.GetID(), unqualified_name, byte_size,
                                   /*context=*/nullptr, LLDB_INVALID_UID,
                                   Type::eEncodingIsUID, decl, compiler_type,
                                   Type::ResolveState::Forward);

  // Remember which DIE backs this (so far incomplete) record so that
  // SymbolFileDWARF::CompleteType can find it and call back into
  // CompleteTypeFromDWARF when the members are needed.
  if (std::optional<DIERef> die_ref = die.GetDIERef())
    dwarf->GetForwardDeclCompilerTypeToDIE().insert_or_assign(
        compiler_type.GetOpaqueQualType(), *die_ref);

  return type_sp;
}

TypeSP DWARFASTParserClike::ParsePointerType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);

  // Resolve the pointee type. A missing DW_AT_type means `void *`, in which
  // case the pointee stays empty. A pointer may point to an incomplete type,
  // so use the forward-declared CompilerType and don't force completion here.
  DWARFDIE pointee_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType pointee_type;
  if (pointee_die)
    if (Type *pointee = die.ResolveTypeUID(pointee_die))
      pointee_type = pointee->GetForwardCompilerType();

  CompilerType pointer_type =
      clike::Builder(m_ts).CreatePointerType(pointee_type);

  Declaration decl = GetDIEDeclaration(die);
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, pointee_die.GetID(),
                         Type::eEncodingIsUID, decl, pointer_type,
                         Type::ResolveState::Full);
}

TypeSP DWARFASTParserClike::ParseTypedef(const DWARFDIE &die) {
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
    clike::Builder ts(m_ts);
    // A missing DW_AT_type means the aliased type is `void`.
    if (!underlying_type)
      underlying_type = ts.GetVoidType();
    typedef_type = ts.CreateTypedefType(name.GetStringRef(), underlying_type,
                                        GetTypeDeclScope(die, ts));
  }

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(typedef_type.GetByteSize(nullptr));

  Declaration decl = GetDIEDeclaration(die);
  return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                         underlying_die.GetID(), Type::eEncodingIsUID, decl,
                         typedef_type, Type::ResolveState::Full);
}

TypeSP DWARFASTParserClike::ParseCVQualifiedType(const DWARFDIE &die) {
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
    clike::Builder ts(m_ts);
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

TypeSP DWARFASTParserClike::ParseAtomicType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  // A DW_TAG_atomic_type (`_Atomic(T)`) has the same size, layout and value as
  // its underlying type T. TypeSystemClike doesn't model an atomic qualifier,
  // so treat it transparently as T -- this is enough for value inspection (e.g.
  // a libc++ `std::atomic`'s `__a_value` member is `_Atomic(int)`).
  DWARFDIE underlying_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType underlying_type;
  if (underlying_die) {
    Type *underlying = die.ResolveTypeUID(underlying_die);
    if (!underlying)
      return nullptr;
    underlying_type = underlying->GetForwardCompilerType();
  }
  if (!underlying_type) {
    clike::Builder ts(m_ts);
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

TypeSP DWARFASTParserClike::ParseEnum(const DWARFDIE &die) {
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
      byte_size =
          llvm::expectedToOptional(underlying_type.GetByteSize(nullptr));
    if (!byte_size)
      byte_size = 4;
  }

  // An enum always has an integer type backing it (see EnumType), so supply
  // the one C would have chosen when the producer recorded none: a signed
  // integer of the recorded width, or plain `int` when there was no width
  // either. Mirrors DWARFASTParserClang::ParseEnum.
  if (!underlying_type)
    underlying_type = m_ts.GetBuiltinTypeForEncodingAndBitSize(
        lldb::eEncodingSint, *byte_size * 8);

  CompilerType enum_type;
  {
    clike::Builder ts(m_ts);
    // Enumerators are few and cheap, so parse them eagerly here rather than
    // deferring to lazy completion.
    std::vector<clike::Enumerator> enumerators;
    bool is_signed = true;
    underlying_type.IsIntegerType(is_signed);
    for (DWARFDIE child : die.children()) {
      if (child.Tag() != DW_TAG_enumerator)
        continue;
      const char *enumerator_name = child.GetName();
      if (!enumerator_name || !enumerator_name[0])
        continue;
      // The raw 64-bit value carries the two's-complement bits for signed
      // enumerators. Bring it to the width of the enumeration, sign-extending
      // it for signed enumerations that are wider than 64 bits.
      uint64_t value = child.GetAttributeValueAsUnsigned(DW_AT_const_value, 0);
      enumerators.emplace_back(ts.GetIdentifier(enumerator_name),
                               llvm::APInt(*byte_size * 8, value, is_signed,
                                           /*implicitTrunc=*/true));
    }
    enum_type = ts.CreateEnumType(
        name.GetStringRef(), byte_size, underlying_type, is_scoped,
        std::move(enumerators), GetTypeDeclScope(die, ts));
  }

  Declaration decl = GetDIEDeclaration(die);
  return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                         underlying_die.GetID(), Type::eEncodingIsUID, decl,
                         enum_type, Type::ResolveState::Full);
}

/// The oldest DWARF version TypeSystemClike knows how to read. See
/// IsSupportedDWARFVersion for why older ones are refused rather than
/// approximated.
static constexpr uint16_t kOldestSupportedDWARFVersion = 4;

bool DWARFASTParserClike::IsSupportedDWARFVersion(const DWARFDIE &die) {
  DWARFUnit *unit = die.GetCU();
  if (!unit)
    return false;
  const uint16_t version = unit->GetVersion();
  if (version >= kOldestSupportedDWARFVersion)
    return true;

  SymbolFileDWARF *dwarf = die.GetDWARF();
  ObjectFile *obj_file = dwarf ? dwarf->GetObjectFile() : nullptr;
  if (Module *module = obj_file ? obj_file->GetModule().get() : nullptr) {
    std::call_once(m_unsupported_version_warning, [&] {
      module->ReportWarning(
          "DWARF version {0} is not supported; types from this module will be "
          "missing. Recompile with -gdwarf-{1} or later, or set "
          "symbols.experimental.enable-typesystem-clike to false.",
          version, kOldestSupportedDWARFVersion);
    });
  }
  return false;
}

TypeSP DWARFASTParserClike::ParseTypeFromDWARF(const SymbolContext &sc,
                                               const DWARFDIE &die,
                                               bool *type_is_new_ptr) {
  if (type_is_new_ptr)
    *type_is_new_ptr = false;

  if (!die)
    return nullptr;

  if (!IsSupportedDWARFVersion(die))
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
  case DW_TAG_pointer_type:
    type_sp = ParsePointerType(die);
    break;
  case DW_TAG_typedef:
  case DW_TAG_template_alias:
    type_sp = ParseTypedef(die);
    break;
  case DW_TAG_const_type:
  case DW_TAG_volatile_type:
    type_sp = ParseCVQualifiedType(die);
    break;
  case DW_TAG_atomic_type:
    type_sp = ParseAtomicType(die);
    break;
  case DW_TAG_enumeration_type:
    type_sp = ParseEnum(die);
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

clike::Type *
DWARFASTParserClike::ResolveReferencedType(const DWARFDIE &referencing_die,
                                           const DWARFDIE &type_die) {
  if (!type_die)
    return nullptr;

  // This drives both the DWARF type bookkeeping (SymbolFileDWARF's DIE->type
  // map) and the TypeSystemClike mutation, hence the Builder.
  clike::Builder ts(m_ts);
  Type *resolved = referencing_die.ResolveTypeUID(type_die);
  if (!resolved)
    return nullptr;
  CompilerType forward = resolved->GetForwardCompilerType();
  return TypeSystemClike::GetClikeType(forward.GetOpaqueQualType());
}

bool DWARFASTParserClike::CompleteTypeFromDWARF(
    const DWARFDIE &die, Type *type, const CompilerType &compiler_type) {
  if (!die)
    return false;

  if (!IsSupportedDWARFVersion(die))
    return false;

  // A DW_TAG_structure/class/union DIE is always backed by a RecordType.
  clike::Type *clike_type =
      TypeSystemClike::GetClikeType(compiler_type.GetOpaqueQualType());
  if (!clike_type || !clike_type->IsAggregate())
    return false;
  auto *record = static_cast<clike::RecordType *>(clike_type);

  // SymbolFileDWARF::CompleteType falls back to the declaration DIE when it
  // can't find a defining DIE anywhere in the debug info (e.g. a type that is
  // only ever forward declared, like `struct FwdClass;`). Such a DIE still
  // carries DW_AT_declaration and has no members to add, so leave the record
  // incomplete instead of marking a declaration-only record complete. This is
  // what lets SBType::IsTypeComplete correctly report a forward-declared type
  // (or the pointee of a pointer to one) as incomplete.
  if (die.GetAttributeValueAsUnsigned(DW_AT_declaration, 0))
    return false;

  // Mark complete up front so that a member whose type (indirectly) refers back
  // to this record doesn't recurse forever, and so that a re-entrant completion
  // of the same record bails out here.
  {
    clike::Builder ts(m_ts);
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
        ts.SetRecordByteSize(*record, *byte_size);
    // An explicit `DW_AT_alignment` (e.g. from `alignas(...)`) cannot be
    // recovered from the size, so record it for GetTypeBitAlign.
    if (std::optional<uint64_t> align =
            die.GetAttributeValueAsOptionalUnsigned(DW_AT_alignment))
      ts.SetRecordExplicitAlignmentInBits(*record, *align * 8);
    ts.SetRecordComplete(*record);

    // DW_AT_calling_convention on the record describes how it is passed and
    // returned by value. Record it so ClangASTGenerator can reproduce the ABI
    // when the expression evaluator calls a function that takes/returns this
    // record by value (e.g. a struct with a non-trivial copy constructor is
    // passed by reference / returned via sret). Mirrors DWARFASTParserClang.
    switch (die.GetAttributeValueAsUnsigned(DW_AT_calling_convention,
                                            llvm::dwarf::DW_CC_normal)) {
    case llvm::dwarf::DW_CC_pass_by_value:
      ts.SetRecordArgPassingKind(
          *record, clike::RecordType::ArgPassingKind::PassByValue);
      break;
    case llvm::dwarf::DW_CC_pass_by_reference:
      ts.SetRecordArgPassingKind(
          *record, clike::RecordType::ArgPassingKind::CannotPassInRegs);
      break;
    default:
      break;
    }
  }

  // Collect the members in declaration order, along with the DWARF reference
  // to each one's type. Only the (heavier) type resolution below is structured
  // to be spread out; this DWARF traversal is not.
  struct MemberInfo {
    enum class Kind { Field, NestedType } kind;
    llvm::StringRef name; // Field/nested-type name; else empty.
    uint64_t byte_offset = 0;
    DWARFDIE referencing_die;
    DWARFDIE type_die;
  };
  std::vector<MemberInfo> members;

  for (DWARFDIE child : die.children()) {
    const dw_tag_t tag = child.Tag();
    if (tag == DW_TAG_member) {
      std::optional<uint64_t> data_member_location =
          child.GetAttributeValueAsOptionalUnsigned(DW_AT_data_member_location);
      members.push_back({MemberInfo::Kind::Field, child.GetName(),
                         data_member_location.value_or(0), child,
                         child.GetAttributeValueAsReferenceDIE(DW_AT_type)});
    } else if (tag == DW_TAG_typedef || tag == DW_TAG_structure_type ||
               tag == DW_TAG_union_type || tag == DW_TAG_enumeration_type) {
      // A type declared inside this record. Resolving the child DIE itself
      // yields the nested type; data formatters look these up by name. Skip
      // anonymous ones.
      const char *nested_name = child.GetName();
      if (nested_name && nested_name[0])
        members.push_back({MemberInfo::Kind::NestedType, nested_name,
                           /*byte_offset=*/0, die, child});
    }
  }

  // Resolve each referenced type. These run inline (deferred) today -- see
  // kMemberResolutionPolicy.
  std::vector<std::future<clike::Type *>> resolved;
  resolved.reserve(members.size());
  for (const MemberInfo &member : members) {
    DWARFDIE referencing_die = member.referencing_die;
    DWARFDIE type_die = member.type_die;
    resolved.push_back(
        std::async(kMemberResolutionPolicy, [this, referencing_die, type_die] {
          return ResolveReferencedType(referencing_die, type_die);
        }));
  }

  // Force each deferred resolution to run, in order.
  std::vector<clike::Type *> member_types(members.size());
  for (size_t i = 0; i < members.size(); ++i)
    member_types[i] = resolved[i].get();

  // Integrate the resolved members into the record, in declaration order.
  clike::Builder ts(m_ts);

  for (size_t i = 0; i < members.size(); ++i) {
    clike::Type *member_type = member_types[i];
    const MemberInfo &member = members[i];
    switch (member.kind) {
    case MemberInfo::Kind::Field:
      // Intern the member name through the type system's Context so its
      // storage is owned alongside the types (rather than pointing into DWARF
      // data).
      if (member_type)
        ts.AddField(*record, ts.GetIdentifier(member.name), member_type,
                    member.byte_offset);
      break;
    case MemberInfo::Kind::NestedType:
      if (member_type)
        ts.AddNestedType(*record, ts.GetIdentifier(member.name), member_type);
      break;
    }
  }

  return true;
}
