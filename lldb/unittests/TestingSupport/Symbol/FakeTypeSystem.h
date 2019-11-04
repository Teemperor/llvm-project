//===-- FakeTypeSystem.h -----------------------------==---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Symbol/Type.h"
#include "lldb/Symbol/TypeSystem.h"

namespace lldb_private {
/// A TypeSystem implementation that throws an error if any of its methods are
/// called. Should be used as a base class for new TypeSystem subclasses in
/// tests.
struct FakeTypeSystem : public TypeSystem {
  // llvm casting support
  static char ID;
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

#define UNIMPLEMENTED_METHOD llvm_unreachable("not implemented")

  ConstString GetPluginName() override { UNIMPLEMENTED_METHOD; }

  uint32_t GetPluginVersion() override { UNIMPLEMENTED_METHOD; }

  ConstString DeclGetName(void *opaque_decl) override { UNIMPLEMENTED_METHOD; }

  CompilerType GetTypeForDecl(void *opaque_decl) override {
    UNIMPLEMENTED_METHOD;
  }

  bool DeclContextIsStructUnionOrClass(void *opaque_decl_ctx) override {
    UNIMPLEMENTED_METHOD;
  }

  ConstString DeclContextGetName(void *opaque_decl_ctx) override {
    UNIMPLEMENTED_METHOD;
  }

  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override {
    UNIMPLEMENTED_METHOD;
  }

  bool
  DeclContextIsClassMethod(void *opaque_decl_ctx,
                           lldb::LanguageType *language_ptr,
                           bool *is_instance_method_ptr,
                           ConstString *language_object_name_ptr) override {
    UNIMPLEMENTED_METHOD;
  }

  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsArrayType(lldb::opaque_compiler_type_t type,
                   CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsAggregateType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsCharType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsCompleteType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsDefined(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsFloatingPointType(lldb::opaque_compiler_type_t type, uint32_t &count,
                           bool &is_complex) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsFunctionType(lldb::opaque_compiler_type_t type,
                      bool *is_variadic_ptr) override {
    UNIMPLEMENTED_METHOD;
  }

  size_t
  GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          const size_t index) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                          CompilerType *function_pointer_type_ptr) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsIntegerType(lldb::opaque_compiler_type_t type,
                     bool &is_signed) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                             CompilerType *target_type, bool check_cplusplus,
                             bool check_objc) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsPointerType(lldb::opaque_compiler_type_t type,
                     CompilerType *pointee_type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsScalarType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsVoidType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool CanPassInRegisters(const CompilerType &type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool SupportsLanguage(lldb::LanguageType language) override {
    UNIMPLEMENTED_METHOD;
  }

  bool GetCompleteType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  uint32_t GetPointerByteSize() override { UNIMPLEMENTED_METHOD; }

  ConstString GetTypeName(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  uint32_t
  GetTypeInfo(lldb::opaque_compiler_type_t type,
              CompilerType *pointee_or_element_compiler_type) override {
    UNIMPLEMENTED_METHOD;
  }

  lldb::LanguageType
  GetMinimumLanguage(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   uint64_t *stride) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType
  GetFunctionReturnType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                           size_t idx) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetPointerType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size) override {
    UNIMPLEMENTED_METHOD;
  }

  llvm::Optional<uint64_t>
  GetBitSize(lldb::opaque_compiler_type_t type,
             ExecutionContextScope *exe_scope) override {
    UNIMPLEMENTED_METHOD;
  }

  lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type,
                             uint64_t &count) override {
    UNIMPLEMENTED_METHOD;
  }

  lldb::Format GetFormat(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  uint32_t GetNumChildren(lldb::opaque_compiler_type_t type,
                          bool omit_empty_base_classes,
                          const ExecutionContext *exe_ctx) override {
    UNIMPLEMENTED_METHOD;
  }

  lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  uint32_t GetNumFields(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                               std::string &name, uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override {
    UNIMPLEMENTED_METHOD;
  }

  uint32_t GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  uint32_t
  GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset_ptr) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset_ptr) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetChildCompilerTypeAtIndex(
      lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override {
    UNIMPLEMENTED_METHOD;
  }

  uint32_t GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                   const char *name,
                                   bool omit_empty_base_classes) override {
    UNIMPLEMENTED_METHOD;
  }

  size_t
  GetIndexOfChildMemberWithName(lldb::opaque_compiler_type_t type,
                                const char *name, bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) override {
    UNIMPLEMENTED_METHOD;
  }

#ifndef NDEBUG
  /// Convenience LLVM-style dump method for use in the debugger only.
  LLVM_DUMP_METHOD void dump(lldb::opaque_compiler_type_t type) const override {
    UNIMPLEMENTED_METHOD;
  }
#endif

  void DumpValue(lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
                 Stream *s, lldb::Format format, const DataExtractor &data,
                 lldb::offset_t data_offset, size_t data_byte_size,
                 uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                 bool show_types, bool show_summary, bool verbose,
                 uint32_t depth) override {
    UNIMPLEMENTED_METHOD;
  }

  bool DumpTypeValue(lldb::opaque_compiler_type_t type, Stream *s,
                     lldb::Format format, const DataExtractor &data,
                     lldb::offset_t data_offset, size_t data_byte_size,
                     uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope) override {
    UNIMPLEMENTED_METHOD;
  }

  void DumpTypeDescription(lldb::opaque_compiler_type_t type) override {}

  void DumpTypeDescription(lldb::opaque_compiler_type_t type,
                           Stream *s) override {}

  bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  void DumpSummary(lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
                   Stream *s, const DataExtractor &data,
                   lldb::offset_t data_offset, size_t data_byte_size) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                CompilerType *pointee_type) override {
    UNIMPLEMENTED_METHOD;
  }

  unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsCStringType(lldb::opaque_compiler_type_t type,
                     uint32_t &length) override {
    UNIMPLEMENTED_METHOD;
  }

  llvm::Optional<size_t>
  GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                  ExecutionContextScope *exe_scope) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsBeingDefined(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }
  bool IsConst(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                  CompilerType *base_type_ptr) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsTypedefType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsVectorType(lldb::opaque_compiler_type_t type,
                    CompilerType *element_type, uint64_t *size) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType
  GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  CompilerType GetNonReferenceType(lldb::opaque_compiler_type_t type) override {
    UNIMPLEMENTED_METHOD;
  }

  bool IsReferenceType(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_type, bool *is_rvalue) override {
    UNIMPLEMENTED_METHOD;
  }
};

#undef UNIMPLEMENTED_METHOD

} // namespace lldb_private
