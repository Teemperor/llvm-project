//===-- MockTypeSystem.h -----------------------------==---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeSystem.h"

using namespace lldb_private;

namespace lldb_testing {
struct MockTypeSystem : public TypeSystem {
  MockTypeSystem() : TypeSystem(TypeSystem::LLVMCastKind::eKindMock) {}

  ConstString GetPluginName() override { return ConstString("MockTypeSystem"); }

  uint32_t GetPluginVersion() override { return 0; }

  ConstString DeclGetName(void *opaque_decl) override { return ConstString(); }

  CompilerType GetTypeForDecl(void *opaque_decl) override {
    return CompilerType();
  }

  bool DeclContextIsStructUnionOrClass(void *opaque_decl_ctx) override {
    return false;
  }

  ConstString DeclContextGetName(void *opaque_decl_ctx) override {
    return ConstString();
  }

  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override {
    return ConstString();
  }

  bool
  DeclContextIsClassMethod(void *opaque_decl_ctx,
                           lldb::LanguageType *language_ptr,
                           bool *is_instance_method_ptr,
                           ConstString *language_object_name_ptr) override {
    return false;
  }

  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override {
    return false;
  }

  bool IsArrayType(lldb::opaque_compiler_type_t type,
                   CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override {
    return false;
  }

  bool IsAggregateType(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  bool IsCharType(lldb::opaque_compiler_type_t type) override { return false; }

  bool IsCompleteType(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  bool IsDefined(lldb::opaque_compiler_type_t type) override { return false; }

  bool IsFloatingPointType(lldb::opaque_compiler_type_t type, uint32_t &count,
                           bool &is_complex) override {
    return false;
  }

  bool IsFunctionType(lldb::opaque_compiler_type_t type,
                      bool *is_variadic_ptr) override {
    return false;
  }

  size_t
  GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) override {
    return 0U;
  }

  CompilerType GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          const size_t index) override {
    return CompilerType();
  }

  bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                          CompilerType *function_pointer_type_ptr) override {
    return false;
  }

  bool IsIntegerType(lldb::opaque_compiler_type_t type,
                     bool &is_signed) override {
    return false;
  }

  bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                             CompilerType *target_type, bool check_cplusplus,
                             bool check_objc) override {
    return false;
  }

  bool IsPointerType(lldb::opaque_compiler_type_t type,
                     CompilerType *pointee_type) override {
    return false;
  }

  bool IsScalarType(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  bool IsVoidType(lldb::opaque_compiler_type_t type) override { return false; }

  bool CanPassInRegisters(const CompilerType &type) override { return false; }

  bool SupportsLanguage(lldb::LanguageType language) override { return false; }

  bool GetCompleteType(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  uint32_t GetPointerByteSize() override { return 0U; }

  ConstString GetTypeName(lldb::opaque_compiler_type_t type) override {
    return ConstString();
  }

  uint32_t
  GetTypeInfo(lldb::opaque_compiler_type_t type,
              CompilerType *pointee_or_element_compiler_type) override {
    return 0U;
  }

  lldb::LanguageType
  GetMinimumLanguage(lldb::opaque_compiler_type_t type) override {
    return lldb::LanguageType::eLanguageTypeUnknown;
  }

  lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) override {
    return lldb::TypeClass::eTypeClassAny;
  }

  CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   uint64_t *stride) override {
    return CompilerType();
  }

  CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) override {
    return CompilerType();
  }

  int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) override {
    return 0;
  }

  CompilerType GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx) override {
    return CompilerType();
  }

  CompilerType
  GetFunctionReturnType(lldb::opaque_compiler_type_t type) override {
    return CompilerType();
  }

  size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) override {
    return 0U;
  }

  TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                           size_t idx) override {
    return TypeMemberFunctionImpl();
  }

  CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) override {
    return CompilerType();
  }

  CompilerType GetPointerType(lldb::opaque_compiler_type_t type) override {
    return CompilerType();
  }

  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size) override {
    return llvm::APFloatBase::EnumToSemantics(
        llvm::APFloatBase::Semantics::S_IEEEdouble);
  }

  llvm::Optional<uint64_t>
  GetBitSize(lldb::opaque_compiler_type_t type,
             ExecutionContextScope *exe_scope) override {
    return llvm::None;
  }

  lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type,
                             uint64_t &count) override {
    return lldb::Encoding::eEncodingInvalid;
  }

  lldb::Format GetFormat(lldb::opaque_compiler_type_t type) override {
    return lldb::Format::eFormatVoid;
  }

  uint32_t GetNumChildren(lldb::opaque_compiler_type_t type,
                          bool omit_empty_base_classes,
                          const ExecutionContext *exe_ctx) override {
    return 0U;
  }

  lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) override {
    return lldb::BasicType::eBasicTypeInt;
  }

  uint32_t GetNumFields(lldb::opaque_compiler_type_t type) override {
    return 0U;
  }

  CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                               std::string &name, uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override {
    return CompilerType();
  }

  uint32_t GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) override {
    return 0U;
  }

  uint32_t
  GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) override {
    return 0U;
  }

  CompilerType GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset_ptr) override {
    return CompilerType();
  }

  CompilerType GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset_ptr) override {
    return CompilerType();
  }

  CompilerType GetChildCompilerTypeAtIndex(
      lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override {
    return CompilerType();
  }

  uint32_t GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                   const char *name,
                                   bool omit_empty_base_classes) override {
    return 0U;
  }

  size_t
  GetIndexOfChildMemberWithName(lldb::opaque_compiler_type_t type,
                                const char *name, bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) override {
    return 0U;
  }

#ifndef NDEBUG
  /// Convenience LLVM-style dump method for use in the debugger only.
  LLVM_DUMP_METHOD void dump(lldb::opaque_compiler_type_t type) const override {
    return;
  }
#endif

  void DumpValue(lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
                 Stream *s, lldb::Format format, const DataExtractor &data,
                 lldb::offset_t data_offset, size_t data_byte_size,
                 uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                 bool show_types, bool show_summary, bool verbose,
                 uint32_t depth) override {}

  bool DumpTypeValue(lldb::opaque_compiler_type_t type, Stream *s,
                     lldb::Format format, const DataExtractor &data,
                     lldb::offset_t data_offset, size_t data_byte_size,
                     uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope) override {
    return false;
  }

  void DumpTypeDescription(lldb::opaque_compiler_type_t type) override {}

  void DumpTypeDescription(lldb::opaque_compiler_type_t type,
                           Stream *s) override {}

  bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  void DumpSummary(lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
                   Stream *s, const DataExtractor &data,
                   lldb::offset_t data_offset, size_t data_byte_size) override {
  }

  bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                CompilerType *pointee_type) override {
    return false;
  }

  unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) override {
    return 0U;
  }

  bool IsCStringType(lldb::opaque_compiler_type_t type,
                     uint32_t &length) override {
    return false;
  }

  llvm::Optional<size_t>
  GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                  ExecutionContextScope *exe_scope) override {
    return llvm::None;
  }

  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override {
    return CompilerType();
  }

  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override {
    return CompilerType();
  }

  bool IsBeingDefined(lldb::opaque_compiler_type_t type) override {
    return false;
  }
  bool IsConst(lldb::opaque_compiler_type_t type) override { return false; }

  uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                  CompilerType *base_type_ptr) override {
    return false;
  }

  bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  bool IsTypedefType(lldb::opaque_compiler_type_t type) override {
    return false;
  }

  CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) override {
    return CompilerType();
  }

  bool IsVectorType(lldb::opaque_compiler_type_t type,
                    CompilerType *element_type, uint64_t *size) override {
    return false;
  }

  CompilerType
  GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) override {
    return CompilerType();
  }

  CompilerType GetNonReferenceType(lldb::opaque_compiler_type_t type) override {
    return CompilerType();
  }

  bool IsReferenceType(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_type, bool *is_rvalue) override {
    return false;
  }
};
} // namespace lldb_testing
