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
#include "DWARFDefines.h"
#include "SymbolFileDWARF.h"

#include "Plugins/TypeSystem/Cpp/Type.h"
#include "Plugins/TypeSystem/Cpp/TypeSystemCpp.h"

#include "lldb/Core/Declaration.h"
#include "lldb/Core/Mangled.h"
#include "lldb/Expression/DWARFExpressionList.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/Language.h"

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
  if (mangled)
    func_name.SetValue(ConstString(mangled));
  else
    func_name.SetValue(ConstString(name));

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

TypeSP DWARFASTParserCpp::ParseBaseType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();
  ConstString name(die.GetName());
  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);
  uint64_t dw_ate = die.GetAttributeValueAsUnsigned(DW_AT_encoding, 0);
  lldb::Encoding encoding = GetEncodingFromDWARF(dw_ate);
  lldb::Format format = GetFormatFromDWARF(dw_ate);

  // Map this base type onto the matching canonical builtin type, falling back
  // to a bespoke type (using the format derived above) if it is not one of the
  // enumerated builtins.
  CompilerType compiler_type =
      m_ts.Lock()->GetBuiltinType(name, byte_size, encoding, format);

  Declaration decl;
  return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
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
  llvm::DWARFTypePrinter<DWARFDIE>(os).appendAndTerminateTemplateParameters(die);
  return params;
}

/// The fully-qualified name of a named type DIE, including enclosing namespace
/// and class scopes (e.g. "std::__1::vector<int, std::__1::allocator<int> >")
/// and reconstructed template arguments. Producing the scoped spelling is what
/// lets type-name-based data formatters (such as libc++'s container summaries)
/// match our types.
static std::string GetDIEQualifiedName(const DWARFDIE &die) {
  std::string name;
  llvm::raw_string_ostream os(name);
  llvm::DWARFTypePrinter<DWARFDIE>(os).appendQualifiedName(die);
  return name;
}

TypeSP DWARFASTParserCpp::ParseStructureType(const DWARFDIE &die) {
  SymbolFileDWARF *dwarf = die.GetDWARF();

  // Record the fully-qualified, template-argument-bearing spelling of the type
  // (e.g. the instantiation "Wrapper<int>" or "std::__1::vector<int, ...>")
  // rather than the bare DW_AT_name ("Wrapper" / "vector").
  ConstString name(GetDIEQualifiedName(die));
  std::optional<uint64_t> byte_size =
      die.GetAttributeValueAsOptionalUnsigned(DW_AT_byte_size);

  // In a C++ translation unit even a `struct` may have base classes, so back
  // records with a ClassType (which reserves storage for that C++-only
  // information). Plain C records use the lighter StructType.
  lldb::LanguageType language = SymbolFileDWARF::GetLanguage(*die.GetCU());
  bool is_cpp_class = Language::LanguageIsCPlusPlus(language);

  CompilerType compiler_type =
      m_ts.Lock()->CreateRecordType(name, byte_size, is_cpp_class);

  Declaration decl;
  TypeSP type_sp =
      dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                      LLDB_INVALID_UID, Type::eEncodingIsUID, decl,
                      compiler_type, Type::ResolveState::Forward);

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
  {
    // Build the (possibly nested) array type(s) under a single lock.
    auto ts = m_ts.Lock();
    if (array_info && !array_info->element_orders.empty()) {
      for (auto it = array_info->element_orders.rbegin(),
                end = array_info->element_orders.rend();
           it != end; ++it)
        array_type = ts->CreateArrayType(array_type, *it);
    } else {
      // No bound information; model it as an array of unknown length.
      array_type = ts->CreateArrayType(array_type, std::nullopt);
    }
  }

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(array_type.GetByteSize(nullptr));

  Declaration decl;
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, element_die.GetID(),
                         Type::eEncodingIsUID, decl, array_type,
                         Type::ResolveState::Full);
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
  if (pointee_die) {
    if (Type *pointee = die.ResolveTypeUID(pointee_die))
      pointee_type = pointee->GetForwardCompilerType();
  }

  CompilerType pointer_type = m_ts.Lock()->CreatePointerType(pointee_type);

  Declaration decl;
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
  // so use the forward-declared CompilerType and don't force completion here.
  DWARFDIE pointee_die = die.GetAttributeValueAsReferenceDIE(DW_AT_type);
  CompilerType pointee_type;
  if (pointee_die) {
    if (Type *pointee = die.ResolveTypeUID(pointee_die))
      pointee_type = pointee->GetForwardCompilerType();
  }

  bool is_rvalue = die.Tag() == DW_TAG_rvalue_reference_type;
  CompilerType reference_type =
      m_ts.Lock()->CreateReferenceType(pointee_type, is_rvalue);

  Declaration decl;
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, pointee_die.GetID(),
                         Type::eEncodingIsUID, decl, reference_type,
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
    if (Type *underlying = die.ResolveTypeUID(underlying_die))
      underlying_type = underlying->GetForwardCompilerType();
  }

  CompilerType typedef_type =
      m_ts.Lock()->CreateTypedefType(name, underlying_type);

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(typedef_type.GetByteSize(nullptr));

  Declaration decl;
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
    if (Type *underlying = die.ResolveTypeUID(underlying_die))
      underlying_type = underlying->GetForwardCompilerType();
  }

  CompilerType cv_type = m_ts.Lock()->CreateCVQualifiedType(
      underlying_type, is_const, is_volatile);

  std::optional<uint64_t> byte_size =
      llvm::expectedToOptional(cv_type.GetByteSize(nullptr));

  Declaration decl;
  ConstString empty_name;
  return dwarf->MakeType(die.GetID(), empty_name, byte_size,
                         /*context=*/nullptr, underlying_die.GetID(),
                         Type::eEncodingIsUID, decl, cv_type,
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

  CompilerType enum_type;
  {
    auto ts = m_ts.Lock();
    enum_type = ts->CreateEnumType(name, byte_size, underlying_type, is_scoped);
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
      ts->AddEnumerator(enum_node, ts->GetIdentifier(enumerator_name), value);
    }
  }

  Declaration decl;
  return dwarf->MakeType(die.GetID(), name, byte_size, /*context=*/nullptr,
                         underlying_die.GetID(), Type::eEncodingIsUID, decl,
                         enum_type, Type::ResolveState::Full);
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
  case DW_TAG_typedef:
    type_sp = ParseTypedef(die);
    break;
  case DW_TAG_const_type:
  case DW_TAG_volatile_type:
    type_sp = ParseCVQualifiedType(die);
    break;
  case DW_TAG_enumeration_type:
    type_sp = ParseEnum(die);
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
  auto ts = m_ts.Lock();
  Type *resolved = referencing_die.ResolveTypeUID(type_die);
  if (!resolved)
    return nullptr;
  CompilerType forward = resolved->GetForwardCompilerType();
  return TypeSystemCpp::GetCppType(forward.GetOpaqueQualType());
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

  // Mark complete up front (under the lock) so that a member whose type
  // (indirectly) refers back to this record doesn't recurse forever, and so
  // that a concurrent completion of the same record bails out here.
  {
    auto ts = m_ts.Lock();
    if (record->IsComplete())
      return true;
    ts->SetRecordComplete(*record);
  }

  // C++-only information (base classes) is only stored on ClassType.
  auto *cpp_class = llvm::dyn_cast<cpp_typesystem::ClassType>(record);

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
      NestedType
    } kind;
    llvm::StringRef name; // Field/nested-type name; empty otherwise.
    uint64_t byte_offset = 0;
    uint64_t value = 0; // Value of a non-type template argument.
    DWARFDIE referencing_die;
    DWARFDIE type_die;
    uint32_t bit_size = 0; // DW_AT_bit_size of a bitfield member (0 if none).
    // DW_AT_data_bit_offset of a bitfield member, or UINT64_MAX if absent.
    uint64_t data_bit_offset = UINT64_MAX;
  };
  std::vector<MemberInfo> members;
  for (DWARFDIE child : die.children()) {
    const dw_tag_t tag = child.Tag();
    if (tag == DW_TAG_inheritance) {
      // Base classes only exist on C++ class types.
      if (!cpp_class)
        continue;
      members.push_back(
          {MemberInfo::Kind::Base, /*name=*/{},
           child.GetAttributeValueAsUnsigned(DW_AT_data_member_location, 0),
           /*value=*/0, child,
           child.GetAttributeValueAsReferenceDIE(DW_AT_type)});
    } else if (tag == DW_TAG_member) {
      members.push_back(
          {MemberInfo::Kind::Field, child.GetName(),
           child.GetAttributeValueAsUnsigned(DW_AT_data_member_location, 0),
           /*value=*/0, child,
           child.GetAttributeValueAsReferenceDIE(DW_AT_type),
           /*bit_size=*/
           static_cast<uint32_t>(
               child.GetAttributeValueAsUnsigned(DW_AT_bit_size, 0)),
           child.GetAttributeValueAsOptionalUnsigned(DW_AT_data_bit_offset)
               .value_or(UINT64_MAX)});
    } else if (tag == DW_TAG_template_type_parameter) {
      members.push_back(
          {MemberInfo::Kind::TemplateType, child.GetName(), /*byte_offset=*/0,
           /*value=*/0, child,
           child.GetAttributeValueAsReferenceDIE(DW_AT_type)});
    } else if (tag == DW_TAG_template_value_parameter) {
      members.push_back(
          {MemberInfo::Kind::TemplateValue, child.GetName(), /*byte_offset=*/0,
           child.GetAttributeValueAsUnsigned(DW_AT_const_value, 0), child,
           child.GetAttributeValueAsReferenceDIE(DW_AT_type)});
    } else if (tag == DW_TAG_typedef || tag == DW_TAG_structure_type ||
               tag == DW_TAG_class_type || tag == DW_TAG_union_type ||
               tag == DW_TAG_enumeration_type) {
      // A type declared inside this record. Resolving the child DIE itself
      // yields the nested type; data formatters look these up by name (e.g. a
      // container's "__node_pointer"). Skip anonymous ones.
      const char *nested_name = child.GetName();
      if (nested_name && nested_name[0])
        members.push_back({MemberInfo::Kind::NestedType, nested_name,
                           /*byte_offset=*/0, /*value=*/0, die, child});
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
  auto ts = m_ts.Lock();
  for (size_t i = 0; i < members.size(); ++i) {
    cpp_typesystem::Type *member_type = member_types[i];
    const MemberInfo &member = members[i];
    switch (member.kind) {
    case MemberInfo::Kind::Base:
      if (member_type)
        ts->AddBaseClass(*cpp_class, member_type, member.byte_offset);
      break;
    case MemberInfo::Kind::Field:
      if (member_type) {
        // For a bitfield, translate the DWARF bit position into the storage
        // unit's byte offset plus the bit offset within it (mirroring
        // TypeSystemClang and what GetChildCompilerTypeAtIndex expects).
        uint64_t byte_offset = member.byte_offset;
        uint32_t bitfield_bit_size = 0;
        uint32_t bitfield_bit_offset = 0;
        if (member.bit_size > 0) {
          bitfield_bit_size = member.bit_size;
          uint64_t abs_bit_offset = member.data_bit_offset != UINT64_MAX
                                        ? member.data_bit_offset
                                        : member.byte_offset * 8;
          uint64_t storage_bits =
              member_type->GetByteSize().value_or(0) * 8;
          if (storage_bits) {
            bitfield_bit_offset = abs_bit_offset % storage_bits;
            byte_offset = (abs_bit_offset - bitfield_bit_offset) / 8;
          } else {
            byte_offset = abs_bit_offset / 8;
          }
        }
        // Intern the member name through the type system's Context so its
        // storage is owned alongside the types (rather than pointing into
        // DWARF data).
        ts->AddField(*record, ts->GetIdentifier(member.name), member_type,
                     byte_offset, bitfield_bit_size, bitfield_bit_offset);
      }
      break;
    case MemberInfo::Kind::TemplateType:
      ts->AddTemplateArgument(
          *record, cpp_typesystem::TemplateArgument{
                       lldb::eTemplateArgumentKindType, member_type, 0});
      break;
    case MemberInfo::Kind::TemplateValue:
      ts->AddTemplateArgument(
          *record, cpp_typesystem::TemplateArgument{
                       lldb::eTemplateArgumentKindIntegral, member_type,
                       member.value});
      break;
    case MemberInfo::Kind::NestedType:
      if (member_type)
        ts->AddNestedType(*record, ts->GetIdentifier(member.name), member_type);
      break;
    }
  }

  return true;
}
