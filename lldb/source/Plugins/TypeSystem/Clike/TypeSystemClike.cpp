//===-- TypeSystemClike.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemClike.h"

#include <optional>

#include "TypeName.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
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
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cstdio>

using namespace lldb_private;
using namespace lldb;

using clike::Field;

using clike::Desugar;

LLDB_PLUGIN_DEFINE(TypeSystemClike)

char TypeSystemClike::ID;

TypeSystemClike::TypeSystemClike(llvm::StringRef name, clike::LanguageOpts opts)
    : m_display_name(name.str()), m_triple(opts.GetTriple()),
      m_context(std::move(opts)) {}

TypeSystemClike::~TypeSystemClike() = default;

CompilerType TypeSystemClike::GetCompilerType(clike::Type *type) {
  return CompilerType(weak_from_this(), type);
}

llvm::Expected<lldb::TypeSystemSP>
TypeSystemClike::Create(llvm::StringRef name, llvm::Triple triple) {
  llvm::Expected<clike::LanguageOpts> opts =
      clike::LanguageOpts::Create(std::move(triple));
  if (!opts)
    return opts.takeError();
  return std::make_shared<TypeSystemClike>(name, std::move(*opts));
}

TypeSystemSP TypeSystemClike::CreateInstance(LanguageType language,
                                             Module *module, Target *target) {
  return TypeSystemSP();
}

LanguageSet TypeSystemClike::GetSupportedLanguagesForTypes() {
  return LanguageSet();
}

LanguageSet TypeSystemClike::GetSupportedLanguagesForExpressions() {
  return LanguageSet();
}

void TypeSystemClike::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "C/C++/Objective-C++ TypeSystem plug-in",
                                CreateInstance, GetSupportedLanguagesForTypes(),
                                GetSupportedLanguagesForExpressions());
}

void TypeSystemClike::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ConstString TypeSystemClike::DeclGetName(void *opaque_decl) {
  // TypeSystemClike doesn't hand out any CompilerDecls yet.
  return ConstString();
}

CompilerType TypeSystemClike::GetTypeForDecl(void *opaque_decl) {
  return CompilerType();
}

ConstString TypeSystemClike::DeclContextGetName(void *opaque_decl_ctx) {
  return ConstString();
}

ConstString
TypeSystemClike::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  return ConstString();
}

bool TypeSystemClike::DeclContextIsClassMethod(void *opaque_decl_ctx) {
  return false;
}

bool TypeSystemClike::DeclContextIsContainedInLookup(
    void *opaque_decl_ctx, void *other_opaque_decl_ctx) {
  return false;
}

LanguageType TypeSystemClike::DeclContextGetLanguage(void *opaque_decl_ctx) {
  return eLanguageTypeC_plus_plus;
}

#ifndef NDEBUG
bool TypeSystemClike::Verify(opaque_compiler_type_t type) { return true; }
#endif

bool TypeSystemClike::IsArrayType(lldb::opaque_compiler_type_t type,
                                  CompilerType *element_type, uint64_t *size,
                                  bool *is_incomplete) {
  return false;
}

bool TypeSystemClike::IsAggregateType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  return t && t->IsAggregate();
}

bool TypeSystemClike::IsCharType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  if (!t)
    return false;
  // Strip typedef/cv sugar so `const char` (e.g. the pointee of `const char *`)
  // is recognized.
  return Desugar(t)->IsChar();
}

bool TypeSystemClike::IsCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // Mirror TypeSystemClang: complete the type now (if it has a definition in
  // the debug info) so we can give the caller an accurate answer about whether
  // the type actually has a definition, rather than just its current internal
  // completeness state. Builtins, pointers, references, etc. report complete
  // via clike::Type::IsComplete(); only records/enums that were parsed
  // as forward declarations without a definition stay incomplete.
  return GetCompleteType(type);
}

bool TypeSystemClike::IsDefined(opaque_compiler_type_t type) { return false; }

bool TypeSystemClike::IsFloatingPointType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  return t && t->GetEncoding() == eEncodingIEEE754;
}

bool TypeSystemClike::IsFunctionType(lldb::opaque_compiler_type_t type) {
  return false;
}

size_t TypeSystemClike::GetNumberOfFunctionArguments(
    lldb::opaque_compiler_type_t type) {
  return 0;
}

CompilerType
TypeSystemClike::GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                            const size_t index) {
  return CompilerType();
}

bool TypeSystemClike::IsFunctionPointerType(lldb::opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsMemberFunctionPointerType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  if (!t)
    return false;
  return Desugar(t)->IsMemberFunctionPointer();
}

bool TypeSystemClike::IsMemberDataPointerType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  if (!t)
    return false;
  return Desugar(t)->IsMemberDataPointer();
}

bool TypeSystemClike::IsBlockPointerType(
    lldb::opaque_compiler_type_t type,
    CompilerType *function_pointer_type_ptr) {
  return false;
}

bool TypeSystemClike::IsIntegerType(opaque_compiler_type_t type,
                                    bool &is_signed) {
  is_signed = false;
  clike::Type *tt = GetClikeType(type);
  return tt && IsIntegerTypeImpl(tt, is_signed);
}

bool TypeSystemClike::IsIntegerTypeImpl(const clike::Type *t, bool &is_signed) {
  // Only builtin integer types (and enumerations, matching TypeSystemClike's
  // existing treatment) are integers; see clike::Type::IsInteger.
  t = Desugar(t);
  if (!t->IsInteger()) {
    is_signed = false;
    return false;
  }
  is_signed = t->GetEncoding() == eEncodingSint;
  return true;
}

bool TypeSystemClike::IsScopedEnumerationType(
    lldb::opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                                            CompilerType *target_type,
                                            bool check_cplusplus,
                                            bool check_objc) {
  return false;
}

bool TypeSystemClike::IsPointerType(lldb::opaque_compiler_type_t type,
                                    CompilerType *pointee_type) {
  return false;
}

bool TypeSystemClike::IsScalarType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  return t && (t->GetTypeInfo() & eTypeIsScalar);
}

bool TypeSystemClike::IsVoidType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  if (!t)
    return false;
  return clike::IsVoid(t);
}

bool TypeSystemClike::CanPassInRegisters(const CompilerType &type) {
  return false;
}

bool TypeSystemClike::SupportsLanguage(LanguageType language) {
  return Language::LanguageIsCFamily(language);
}

bool TypeSystemClike::GetCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  clike::Type *t = GetClikeType(type);
  clike::Type *completed = CompleteTypeImpl(t);
  return completed && completed->IsComplete();
}

clike::Type *TypeSystemClike::CompleteTypeImpl(clike::Type *type) {
  if (!type)
    return nullptr;
  // See through typedefs/qualifiers: it is the underlying record that carries
  // completion state and is registered in the forward-declaration map.
  clike::Type *t = Desugar(type);
  if (!t)
    return nullptr;
  if (t->IsComplete())
    return t;
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
  return t;
}

uint32_t TypeSystemClike::GetPointerByteSize() {
  return m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size;
}

CompilerType TypeSystemClike::GetSizeType() {
  // The type of a `sizeof` result. Clang spells this builtin `__size_t`, sized
  // as a pointer, so model it as a bespoke builtin of that name and width --
  // exactly as GetPointerDiffType does for `__ptrdiff_t` -- rather than as the
  // canonical unsigned type of the same width, so a value object created from
  // it reports the name callers expect.
  clike::Builder builder(*this);
  return builder.GetBuiltinType(
      "__size_t", m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size,
      lldb::eEncodingUint, lldb::eFormatUnsigned);
}

CompilerType TypeSystemClike::GetPointerDiffType(bool is_signed) {
  // The result of pointer subtraction. Clang spells this builtin `__ptrdiff_t`
  // (and its unsigned counterpart), sized as a pointer. Model it as a bespoke
  // builtin of pointer width so value objects created from it show that name.
  const uint64_t byte_size =
      m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size;
  clike::Builder builder(*this);
  if (is_signed)
    return builder.GetBuiltinType("__ptrdiff_t", byte_size, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  return builder.GetBuiltinType("__ptrdiff_t unsigned", byte_size,
                                lldb::eEncodingUint, lldb::eFormatUnsigned);
}

unsigned TypeSystemClike::GetPtrAuthKey(lldb::opaque_compiler_type_t type) {
  return 0;
}

unsigned
TypeSystemClike::GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t type) {
  return 0;
}

bool TypeSystemClike::GetPtrAuthAddressDiversity(
    lldb::opaque_compiler_type_t type) {
  return false;
}

ConstString TypeSystemClike::GetTypeName(opaque_compiler_type_t type,
                                         bool BaseOnly) {
  if (!type)
    return ConstString();
  return ConstString(clike::BuildCanonicalName(GetClikeType(type), BaseOnly));
}

ConstString TypeSystemClike::GetDisplayTypeName(opaque_compiler_type_t type) {
  if (!type)
    return ConstString();
  return ConstString(clike::BuildDisplayName(GetClikeType(type)));
}

uint32_t
TypeSystemClike::GetTypeInfo(opaque_compiler_type_t type,
                             CompilerType *pointee_or_element_compiler_type) {
  if (pointee_or_element_compiler_type)
    pointee_or_element_compiler_type->Clear();
  clike::Type *tt = GetClikeType(type);
  if (!tt)
    return 0;
  const clike::Type *t = tt;
  return t->GetTypeInfo();
}

LanguageType TypeSystemClike::GetMinimumLanguage(opaque_compiler_type_t type) {
  // TypeSystemClike models C/C++/Objective-C++ types. Records report C++,
  // which is what puts them in the C++ formatter category, so that e.g. the
  // libc++ container data formatters are consulted (see
  // FormatManager::GetCandidateLanguages); see clike::Type::GetMinimumLanguage
  // for the rest.
  clike::Type *tt = GetClikeType(type);
  if (tt)
    return Desugar(tt)->GetMinimumLanguage();
  return eLanguageTypeC_plus_plus;
}

TypeClass TypeSystemClike::GetTypeClass(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  return t ? t->GetTypeClass() : eTypeClassInvalid;
}

CompilerType
TypeSystemClike::GetArrayElementType(lldb::opaque_compiler_type_t type,
                                     ExecutionContextScope *exe_scope) {
  return CompilerType();
}

CompilerType TypeSystemClike::GetCanonicalType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  if (!t)
    return CompilerType();
  // The canonical type is the type with all typedef/cv sugar stripped.
  return GetCompilerType(Desugar(t));
}

CompilerType
TypeSystemClike::GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

int TypeSystemClike::GetFunctionArgumentCount(
    lldb::opaque_compiler_type_t type) {
  return 0;
}

CompilerType TypeSystemClike::GetFunctionArgumentTypeAtIndex(
    lldb::opaque_compiler_type_t type, size_t idx) {
  return CompilerType();
}

CompilerType
TypeSystemClike::GetFunctionReturnType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

size_t TypeSystemClike::GetNumMemberFunctions(opaque_compiler_type_t type) {
  // C records have no member functions.
  return 0;
}

TypeMemberFunctionImpl
TypeSystemClike::GetMemberFunctionAtIndex(opaque_compiler_type_t type,
                                          size_t idx) {
  return TypeMemberFunctionImpl();
}

CompilerType
TypeSystemClike::GetPointeeType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType
TypeSystemClike::GetPointerType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

const llvm::fltSemantics &
TypeSystemClike::GetFloatTypeSemantics(size_t byte_size, Format format) {
  return m_context.GetLanguageOpts().GetFloatTypeSemantics(byte_size, format);
}

llvm::Expected<uint64_t>
TypeSystemClike::GetBitSize(opaque_compiler_type_t type,
                            ExecutionContextScope *exe_scope) {
  clike::Type *tt = GetClikeType(type);
  if (!tt)
    return llvm::createStringError("invalid type");
  const clike::Type *t = Desugar(tt);
  if (std::optional<uint64_t> byte_size = t->GetByteSize())
    return *byte_size * 8;
  return llvm::createStringError("TypeSystemClike::GetBitSize: unknown size");
}

Encoding TypeSystemClike::GetEncoding(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  return t ? t->GetEncoding() : eEncodingInvalid;
}

Format TypeSystemClike::GetFormat(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  return t ? t->GetFormat() : eFormatDefault;
}

llvm::Expected<uint32_t>
TypeSystemClike::GetNumChildren(lldb::opaque_compiler_type_t type,
                                bool omit_empty_base_classes,
                                const ExecutionContext *exe_ctx) {
  return 0;
}

BasicType
TypeSystemClike::GetBasicTypeEnumeration(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  if (!t)
    return eBasicTypeInvalid;
  return Desugar(t)->GetBasicTypeEnumeration();
}

bool TypeSystemClike::IsPromotableIntegerType(opaque_compiler_type_t type) {
  clike::Type *t = GetClikeType(type);
  return t && IsPromotableIntegerTypeImpl(t);
}

bool TypeSystemClike::IsPromotableIntegerTypeImpl(const clike::Type *t) {
  // Follows C++ [conv.prom]: integer types with a conversion rank less than
  // int, plus bool/character/unscoped-enum types, promote to int/unsigned int.
  return Desugar(t)->IsPromotableInteger();
}

CompilerType
TypeSystemClike::GetPromotedIntegerType(opaque_compiler_type_t type) {
  clike::Type *tt = GetClikeType(type);
  if (!tt || !IsPromotableIntegerTypeImpl(tt))
    return CompilerType();

  CompilerType int_type = GetBasicTypeFromAST(eBasicTypeInt);
  std::optional<uint64_t> int_size =
      GetClikeType(int_type.GetOpaqueQualType())->GetByteSize();

  // The result of integer promotion is `int` if int can represent all values
  // of the source type, otherwise `unsigned int`. Compare byte sizes and
  // signedness against int.
  std::optional<uint64_t> src_size = tt->GetByteSize();

  bool is_signed = false;
  bool src_is_integer = IsIntegerTypeImpl(tt, is_signed);

  if (src_size && int_size && *src_size >= *int_size && src_is_integer &&
      !is_signed) {
    // An unsigned source that is at least as wide as int cannot be represented
    // by int; promote to unsigned int instead.
    return GetBasicTypeFromAST(eBasicTypeUnsignedInt);
  }
  return int_type;
}

uint32_t TypeSystemClike::GetNumFields(lldb::opaque_compiler_type_t type) {
  return 0;
}

CompilerType TypeSystemClike::GetFieldAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx, std::string &name,
                                              uint64_t *bit_offset_ptr,
                                              uint32_t *bitfield_bit_size_ptr,
                                              bool *is_bitfield_ptr) {
  return CompilerType();
}

uint32_t TypeSystemClike::GetNumDirectBaseClasses(opaque_compiler_type_t type) {
  // C records have no base classes.
  return 0;
}

uint32_t
TypeSystemClike::GetNumVirtualBaseClasses(opaque_compiler_type_t type) {
  return 0;
}

CompilerType TypeSystemClike::GetDirectBaseClassAtIndex(
    opaque_compiler_type_t type, size_t idx, uint32_t *bit_offset_ptr) {
  return CompilerType();
}

CompilerType TypeSystemClike::GetVirtualBaseClassAtIndex(
    opaque_compiler_type_t type, size_t idx, uint32_t *bit_offset_ptr) {
  return CompilerType();
}

llvm::Expected<CompilerType> TypeSystemClike::GetDereferencedType(
    lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
    std::string &deref_name, uint32_t &deref_byte_size,
    int32_t &deref_byte_offset, ValueObject *valobj, uint64_t &language_flags) {
  return CompilerType();
}

llvm::Expected<CompilerType> TypeSystemClike::GetChildCompilerTypeAtIndex(
    lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {
  return CompilerType();
}

llvm::Expected<uint32_t>
TypeSystemClike::GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                         llvm::StringRef name,
                                         bool omit_empty_base_classes) {
  return 0;
}

size_t TypeSystemClike::GetIndexOfChildMemberWithName(
    lldb::opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  return 0;
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void TypeSystemClike::dump(opaque_compiler_type_t type) const {
}
#endif

bool TypeSystemClike::DumpTypeValue(opaque_compiler_type_t type, Stream &s,
                                    Format format, const DataExtractor &data,
                                    offset_t data_offset, size_t data_byte_size,
                                    uint32_t bitfield_bit_size,
                                    uint32_t bitfield_bit_offset,
                                    ExecutionContextScope *exe_scope) {
  clike::Type *tt = GetClikeType(type);
  if (!tt)
    return false;
  const clike::Type *t = Desugar(tt);
  // Aggregates don't have a scalar value to print; their children are dumped
  // individually.
  if (t->IsAggregate())
    return false;
  if (format == eFormatDefault)
    format = t->GetFormat();
  // Some formats dump the value as a sequence of smaller items rather than one
  // scalar: e.g. the char/bytes formats print each byte, and the unicode
  // formats print each code unit. Split the byte size into that many items so
  // e.g. a 16-byte `__uint128_t` printed with a char format is shown as 16
  // characters instead of being rejected as too wide (matching
  // TypeSystemClang).
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

void TypeSystemClike::DumpTypeDescription(opaque_compiler_type_t type,
                                          DescriptionLevel level) {
  StreamFile s(stdout, false);
  DumpTypeDescription(type, s, level);
}

void TypeSystemClike::DumpTypeDescription(opaque_compiler_type_t type,
                                          Stream &s, DescriptionLevel level) {
  clike::Type *tt = GetClikeType(type);
  if (!tt)
    return;
  clike::Type *t = Desugar(tt);

  // Anything else: just print its name.
  if (ConstString name = GetTypeName(t, /*BaseOnly=*/false))
    s.PutCString(name.GetStringRef());
}

void TypeSystemClike::Dump(llvm::raw_ostream &output, llvm::StringRef filter,
                           bool show_color) {
  // Collect every record type this system has produced and hand them to the
  // Clang-AST synthesizer (which lives in the expression-parser plugin, since
  // TypeSystem/Clike must not depend on the clang AST) to build and print a
  // throwaway clang AST. Backs `target modules dump ast`.
  // The synthesizer that turns those records into a printable clang AST lives
  // in the expression-parser plugin and arrives with ClangASTGenerator; until
  // then there is nothing to print.
}

bool TypeSystemClike::IsRuntimeGeneratedType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsPointerOrReferenceType(
    lldb::opaque_compiler_type_t type, CompilerType *pointee_type) {
  return false;
}

unsigned TypeSystemClike::GetTypeQualifiers(lldb::opaque_compiler_type_t type) {
  return 0;
}

std::optional<size_t>
TypeSystemClike::GetTypeBitAlign(opaque_compiler_type_t type,
                                 ExecutionContextScope *exe_scope) {
  clike::Type *tt = GetClikeType(type);
  if (!tt)
    return std::nullopt;
  const clike::Type *t = Desugar(tt);
  return t ? t->GetAlignmentInBits() : std::nullopt;
}

CompilerType TypeSystemClike::GetBuiltinTypeByName(ConstString name) {
  llvm::StringRef name_ref = name.GetStringRef();

  // `_BitInt(N)` / `unsigned _BitInt(N)` are synthesized on demand (they never
  // appear as an enumerated builtin spelling). Mirror TypeSystemClang: parse
  // the bit width and lay the type out with the target's ABI size.
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
        m_context.GetBuiltinType(clike::BuiltinKind::Int128));
  if (name_ref == "__uint128_t")
    return GetCompilerType(
        m_context.GetBuiltinType(clike::BuiltinKind::UnsignedInt128));

  if (clike::BuiltinType *bt =
          m_context.GetBuiltinTypeByName(name.GetStringRef()))
    return GetCompilerType(bt);
  return CompilerType();
}

CompilerType TypeSystemClike::GetBasicTypeFromAST(BasicType basic_type) {
  // Everything the type system models is one of the enumerated builtins.
  // Anything it doesn't (e.g. eBasicTypeHalf) has no basic type here.
  std::optional<clike::BuiltinKind> kind =
      clike::KnownBuiltinTypes::KindForBasicType(basic_type);
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
}

CompilerType
TypeSystemClike::GetBuiltinTypeForEncodingAndBitSize(Encoding encoding,
                                                     size_t bit_size) {
  std::optional<clike::BuiltinKind> kind =
      clike::KnownBuiltinTypes::KindForEncodingAndBitSize(encoding, bit_size);
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
}

bool TypeSystemClike::IsBeingDefined(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsConst(lldb::opaque_compiler_type_t type) {
  return false;
}

uint32_t
TypeSystemClike::IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                        CompilerType *base_type_ptr) {
  return 0;
}

bool TypeSystemClike::IsPolymorphicClass(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsTypedefType(lldb::opaque_compiler_type_t type) {
  return false;
}

CompilerType
TypeSystemClike::GetTypedefedType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

bool TypeSystemClike::IsVectorType(lldb::opaque_compiler_type_t type,
                                   CompilerType *element_type, uint64_t *size) {
  return false;
}

CompilerType
TypeSystemClike::GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystemClike::GetNonReferenceType(opaque_compiler_type_t type) {
  clike::Type *tt = GetClikeType(type);
  if (!tt)
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
  const clike::Type *t = Desugar(tt);
  if (t->IsReference())
    return GetCompilerType(t->GetPointeeType());
  return CompilerType(weak_from_this(), type);
}

bool TypeSystemClike::IsReferenceType(opaque_compiler_type_t type,
                                      CompilerType *pointee_type,
                                      bool *is_rvalue) {
  if (pointee_type)
    pointee_type->Clear();
  if (is_rvalue)
    *is_rvalue = false;
  clike::Type *tt = GetClikeType(type);
  if (!tt)
    return false;
  const clike::Type *ref = Desugar(tt);
  if (!ref->IsReference())
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(ref->GetPointeeType());
  if (is_rvalue)
    *is_rvalue = ref->IsRValueReference();
  return true;
}
