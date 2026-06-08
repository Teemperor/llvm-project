//===-- TypeSystemCpp.cpp -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemCpp.h"

#include "lldb/Core/PluginManager.h"

using namespace lldb_private;
using namespace lldb;

LLDB_PLUGIN_DEFINE(TypeSystemCpp)

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

bool TypeSystemCpp::isA(const void *ClassID) const { return false; }

ConstString TypeSystemCpp::DeclGetName(void *opaque_decl) {
  return ConstString();
}

CompilerType TypeSystemCpp::GetTypeForDecl(void *opaque_decl) {
  return CompilerType();
}

ConstString TypeSystemCpp::DeclContextGetName(void *opaque_decl_ctx) {
  return ConstString();
}

ConstString
TypeSystemCpp::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  return ConstString();
}

bool TypeSystemCpp::DeclContextIsClassMethod(void *opaque_decl_ctx) {
  return false;
}

bool TypeSystemCpp::DeclContextIsContainedInLookup(
    void *opaque_decl_ctx, void *other_opaque_decl_ctx) {
  return false;
}

LanguageType TypeSystemCpp::DeclContextGetLanguage(void *opaque_decl_ctx) {
  return eLanguageTypeUnknown;
}

#ifndef NDEBUG
bool TypeSystemCpp::Verify(opaque_compiler_type_t type) { return true; }
#endif

bool TypeSystemCpp::IsArrayType(opaque_compiler_type_t type,
                                CompilerType *element_type, uint64_t *size,
                                bool *is_incomplete) {
  return false;
}

bool TypeSystemCpp::IsAggregateType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsCharType(opaque_compiler_type_t type) { return false; }

bool TypeSystemCpp::IsCompleteType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsDefined(opaque_compiler_type_t type) { return false; }

bool TypeSystemCpp::IsFloatingPointType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsFunctionType(opaque_compiler_type_t type) {
  return false;
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
  return false;
}

bool TypeSystemCpp::IsMemberFunctionPointerType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsMemberDataPointerType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsBlockPointerType(
    opaque_compiler_type_t type, CompilerType *function_pointer_type_ptr) {
  return false;
}

bool TypeSystemCpp::IsIntegerType(opaque_compiler_type_t type,
                                  bool &is_signed) {
  is_signed = false;
  return false;
}

bool TypeSystemCpp::IsScopedEnumerationType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsPossibleDynamicType(opaque_compiler_type_t type,
                                          CompilerType *target_type,
                                          bool check_cplusplus,
                                          bool check_objc) {
  return false;
}

bool TypeSystemCpp::IsPointerType(opaque_compiler_type_t type,
                                  CompilerType *pointee_type) {
  return false;
}

bool TypeSystemCpp::IsScalarType(opaque_compiler_type_t type) { return false; }

bool TypeSystemCpp::IsVoidType(opaque_compiler_type_t type) { return false; }

bool TypeSystemCpp::CanPassInRegisters(const CompilerType &type) {
  return false;
}

bool TypeSystemCpp::SupportsLanguage(LanguageType language) { return false; }

bool TypeSystemCpp::GetCompleteType(opaque_compiler_type_t type) {
  return false;
}

uint32_t TypeSystemCpp::GetPointerByteSize() { return 0; }

CompilerType TypeSystemCpp::GetPointerDiffType(bool is_signed) {
  return CompilerType();
}

unsigned TypeSystemCpp::GetPtrAuthKey(opaque_compiler_type_t type) { return 0; }

unsigned TypeSystemCpp::GetPtrAuthDiscriminator(opaque_compiler_type_t type) {
  return 0;
}

bool TypeSystemCpp::GetPtrAuthAddressDiversity(opaque_compiler_type_t type) {
  return false;
}

ConstString TypeSystemCpp::GetTypeName(opaque_compiler_type_t type,
                                       bool BaseOnly) {
  return ConstString();
}

ConstString TypeSystemCpp::GetDisplayTypeName(opaque_compiler_type_t type) {
  return ConstString();
}

uint32_t
TypeSystemCpp::GetTypeInfo(opaque_compiler_type_t type,
                           CompilerType *pointee_or_element_compiler_type) {
  return 0;
}

LanguageType TypeSystemCpp::GetMinimumLanguage(opaque_compiler_type_t type) {
  return eLanguageTypeUnknown;
}

TypeClass TypeSystemCpp::GetTypeClass(opaque_compiler_type_t type) {
  return eTypeClassInvalid;
}

CompilerType
TypeSystemCpp::GetArrayElementType(opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) {
  return CompilerType();
}

CompilerType TypeSystemCpp::GetCanonicalType(opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType
TypeSystemCpp::GetEnumerationIntegerType(opaque_compiler_type_t type) {
  return CompilerType();
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
  return CompilerType();
}

CompilerType TypeSystemCpp::GetPointerType(opaque_compiler_type_t type) {
  return CompilerType();
}

const llvm::fltSemantics &
TypeSystemCpp::GetFloatTypeSemantics(size_t byte_size, Format format) {
  return llvm::APFloat::Bogus();
}

llvm::Expected<uint64_t>
TypeSystemCpp::GetBitSize(opaque_compiler_type_t type,
                          ExecutionContextScope *exe_scope) {
  return llvm::createStringError("TypeSystemCpp::GetBitSize not implemented");
}

Encoding TypeSystemCpp::GetEncoding(opaque_compiler_type_t type) {
  return eEncodingInvalid;
}

Format TypeSystemCpp::GetFormat(opaque_compiler_type_t type) {
  return eFormatDefault;
}

llvm::Expected<uint32_t>
TypeSystemCpp::GetNumChildren(opaque_compiler_type_t type,
                              bool omit_empty_base_classes,
                              const ExecutionContext *exe_ctx) {
  return 0;
}

BasicType
TypeSystemCpp::GetBasicTypeEnumeration(opaque_compiler_type_t type) {
  return eBasicTypeInvalid;
}

uint32_t TypeSystemCpp::GetNumFields(opaque_compiler_type_t type) { return 0; }

CompilerType TypeSystemCpp::GetFieldAtIndex(opaque_compiler_type_t type,
                                            size_t idx, std::string &name,
                                            uint64_t *bit_offset_ptr,
                                            uint32_t *bitfield_bit_size_ptr,
                                            bool *is_bitfield_ptr) {
  return CompilerType();
}

uint32_t
TypeSystemCpp::GetNumDirectBaseClasses(opaque_compiler_type_t type) {
  return 0;
}

uint32_t
TypeSystemCpp::GetNumVirtualBaseClasses(opaque_compiler_type_t type) {
  return 0;
}

CompilerType
TypeSystemCpp::GetDirectBaseClassAtIndex(opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset_ptr) {
  return CompilerType();
}

CompilerType
TypeSystemCpp::GetVirtualBaseClassAtIndex(opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset_ptr) {
  return CompilerType();
}

llvm::Expected<CompilerType> TypeSystemCpp::GetDereferencedType(
    opaque_compiler_type_t type, ExecutionContext *exe_ctx,
    std::string &deref_name, uint32_t &deref_byte_size,
    int32_t &deref_byte_offset, ValueObject *valobj,
    uint64_t &language_flags) {
  return CompilerType();
}

llvm::Expected<CompilerType> TypeSystemCpp::GetChildCompilerTypeAtIndex(
    opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {
  return CompilerType();
}

llvm::Expected<uint32_t>
TypeSystemCpp::GetIndexOfChildWithName(opaque_compiler_type_t type,
                                       llvm::StringRef name,
                                       bool omit_empty_base_classes) {
  return llvm::createStringError(
      "TypeSystemCpp::GetIndexOfChildWithName not implemented");
}

size_t TypeSystemCpp::GetIndexOfChildMemberWithName(
    opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  return 0;
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void
TypeSystemCpp::dump(opaque_compiler_type_t type) const {}
#endif

bool TypeSystemCpp::DumpTypeValue(opaque_compiler_type_t type, Stream &s,
                                  Format format, const DataExtractor &data,
                                  offset_t data_offset, size_t data_byte_size,
                                  uint32_t bitfield_bit_size,
                                  uint32_t bitfield_bit_offset,
                                  ExecutionContextScope *exe_scope) {
  return false;
}

void TypeSystemCpp::DumpTypeDescription(opaque_compiler_type_t type,
                                        DescriptionLevel level) {}

void TypeSystemCpp::DumpTypeDescription(opaque_compiler_type_t type, Stream &s,
                                        DescriptionLevel level) {}

void TypeSystemCpp::Dump(llvm::raw_ostream &output, llvm::StringRef filter,
                         bool show_color) {}

bool TypeSystemCpp::IsRuntimeGeneratedType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsPointerOrReferenceType(opaque_compiler_type_t type,
                                             CompilerType *pointee_type) {
  return false;
}

unsigned TypeSystemCpp::GetTypeQualifiers(opaque_compiler_type_t type) {
  return 0;
}

std::optional<size_t>
TypeSystemCpp::GetTypeBitAlign(opaque_compiler_type_t type,
                               ExecutionContextScope *exe_scope) {
  return std::nullopt;
}

CompilerType TypeSystemCpp::GetBasicTypeFromAST(BasicType basic_type) {
  return CompilerType();
}

CompilerType
TypeSystemCpp::GetBuiltinTypeForEncodingAndBitSize(Encoding encoding,
                                                   size_t bit_size) {
  return CompilerType();
}

bool TypeSystemCpp::IsBeingDefined(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsConst(opaque_compiler_type_t type) { return false; }

uint32_t TypeSystemCpp::IsHomogeneousAggregate(opaque_compiler_type_t type,
                                               CompilerType *base_type_ptr) {
  return 0;
}

bool TypeSystemCpp::IsPolymorphicClass(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemCpp::IsTypedefType(opaque_compiler_type_t type) { return false; }

CompilerType TypeSystemCpp::GetTypedefedType(opaque_compiler_type_t type) {
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
  return CompilerType();
}

bool TypeSystemCpp::IsReferenceType(opaque_compiler_type_t type,
                                    CompilerType *pointee_type,
                                    bool *is_rvalue) {
  return false;
}
