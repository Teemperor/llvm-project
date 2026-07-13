//===-- Builder.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemCpp.h"

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;
using namespace lldb;

Builder::Builder(TypeSystemCpp &ts) : m_ts(ts), m_lock(ts.m_mutex) {}

TypeRef Builder::ToTypeRef(const CompilerType &type) {
  auto ts = type.GetTypeSystem().dyn_cast_or_null<TypeSystemCpp>();
  if (!ts)
    return TypeRef();
  return TypeRef(ts->m_context,
                 TypeSystemCpp::GetCppType(type.GetOpaqueQualType()));
}

TypeRef Builder::ToTypeRef(Type *type) const {
  return TypeRef(m_ts.m_context, type);
}

CompilerType Builder::GetBuiltinType(ConstString name,
                                     std::optional<uint64_t> byte_size,
                                     lldb::Encoding encoding,
                                     lldb::Format format) {
  return m_ts.GetCompilerType(m_ts.m_context.GetBuiltinType(
      name.GetStringRef(), byte_size, encoding, format));
}

CompilerType Builder::GetVoidType() {
  return m_ts.GetCompilerType(
      m_ts.m_context.GetBuiltinType(cpp_typesystem::BuiltinKind::Void));
}

CompilerType Builder::CreateRecordType(ConstString name,
                                       std::optional<uint64_t> byte_size,
                                       bool is_cpp_class, bool is_union) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateRecordType(
      name.GetStringRef(), byte_size, is_cpp_class, is_union));
}

CompilerType Builder::CreateArrayType(CompilerType element_type,
                                      std::optional<uint64_t> num_elements) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateArrayType(ToTypeRef(element_type), num_elements));
}

CompilerType Builder::CreatePointerType(CompilerType pointee_type) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreatePointerType(ToTypeRef(pointee_type)));
}

CompilerType Builder::CreateReferenceType(CompilerType pointee_type,
                                          bool is_rvalue) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateReferenceType(ToTypeRef(pointee_type), is_rvalue));
}

CompilerType Builder::CreateTypedefType(ConstString name,
                                        CompilerType underlying_type) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateTypedefType(
      name.GetStringRef(), ToTypeRef(underlying_type)));
}

CompilerType Builder::CreateCVQualifiedType(CompilerType underlying_type,
                                            bool is_const, bool is_volatile) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateCVQualifiedType(
      ToTypeRef(underlying_type), is_const, is_volatile));
}

CompilerType Builder::CreateEnumType(ConstString name,
                                     std::optional<uint64_t> byte_size,
                                     CompilerType underlying_type,
                                     bool is_scoped) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateEnumType(
      name.GetStringRef(), byte_size, ToTypeRef(underlying_type), is_scoped));
}

cpp_typesystem::Identifier Builder::GetIdentifier(llvm::StringRef name) {
  return m_ts.m_context.GetIdentifier(name);
}

void Builder::SetRecordComplete(cpp_typesystem::RecordType &record) {
  m_ts.m_context.SetComplete(record);
}

void Builder::AddField(cpp_typesystem::RecordType &record,
                       cpp_typesystem::Identifier name,
                       cpp_typesystem::Type *type, uint64_t byte_offset,
                       uint32_t bitfield_bit_size,
                       uint32_t bitfield_bit_offset) {
  m_ts.m_context.AddField(record, name, ToTypeRef(type), byte_offset,
                          bitfield_bit_size, bitfield_bit_offset);
}

void Builder::AddBaseClass(cpp_typesystem::ClassType &record,
                           cpp_typesystem::Type *type, uint64_t byte_offset) {
  m_ts.m_context.AddBaseClass(record, ToTypeRef(type), byte_offset);
}

void Builder::AddEnumerator(cpp_typesystem::EnumType &enum_type,
                            cpp_typesystem::Identifier name, uint64_t value) {
  m_ts.m_context.AddEnumerator(enum_type, name, value);
}

void Builder::AddTemplateArgument(cpp_typesystem::RecordType &record,
                                  lldb::TemplateArgumentKind kind,
                                  cpp_typesystem::Type *type,
                                  uint64_t integral_value) {
  m_ts.m_context.AddTemplateArgument(
      record, cpp_typesystem::TemplateArgument{kind, ToTypeRef(type),
                                               integral_value});
}

void Builder::AddNestedType(cpp_typesystem::RecordType &record,
                            cpp_typesystem::Identifier name,
                            cpp_typesystem::Type *type) {
  m_ts.m_context.AddNestedType(record, name, ToTypeRef(type));
}
