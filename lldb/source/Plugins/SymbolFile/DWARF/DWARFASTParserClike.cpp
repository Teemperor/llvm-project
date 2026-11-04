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
#include "DWARFDefines.h"
#include "DWARFUnit.h"
#include "SymbolFileDWARF.h"

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/Type.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "lldb/Core/Declaration.h"
#include "lldb/Core/Module.h"
#include "lldb/Expression/Expression.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeMap.h"

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"

#include <algorithm>

using namespace lldb;
using namespace lldb_private;
using namespace lldb_private::plugin::dwarf;
using namespace llvm::dwarf;

DWARFASTParserClike::DWARFASTParserClike(TypeSystemClike &ts)
    : DWARFASTParser(Kind::DWARFASTParserClike), m_ts(ts) {}

DWARFASTParserClike::~DWARFASTParserClike() = default;

Function *DWARFASTParserClike::ParseFunctionFromDWARF(
    CompileUnit &comp_unit, const DWARFDIE &die, AddressRanges func_ranges) {
  // Functions aren't parsed yet.
  return nullptr;
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

ConstString
DWARFASTParserClike::ConstructDemangledNameFromDWARF(const DWARFDIE &die) {
  // Functions aren't parsed yet.
  return ConstString();
}

CompilerDeclContext
DWARFASTParserClike::GetDeclContextForUIDFromDWARF(const DWARFDIE &die) {
  return CompilerDeclContext();
}

CompilerDeclContext
DWARFASTParserClike::GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) {
  return CompilerDeclContext();
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

bool DWARFASTParserClike::CompleteTypeFromDWARF(
    const DWARFDIE &die, Type *type, const CompilerType &compiler_type) {
  return false;
}
