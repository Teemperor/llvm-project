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

CompilerType Builder::GetBuiltinType(llvm::StringRef name,
                                     std::optional<uint64_t> byte_size,
                                     lldb::Encoding encoding,
                                     lldb::Format format) {
  return m_ts.GetCompilerType(
      m_ts.m_context.GetBuiltinType(name, byte_size, encoding, format));
}

CompilerType Builder::GetVoidType() {
  return m_ts.GetCompilerType(
      m_ts.m_context.GetBuiltinType(cpp_typesystem::BuiltinKind::Void));
}

CompilerType Builder::CreateRecordType(llvm::StringRef name,
                                       std::optional<uint64_t> byte_size,
                                       bool is_cpp_class, bool is_union,
                                       bool is_class_keyword) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateRecordType(
      name, byte_size, is_cpp_class, is_union, is_class_keyword));
}

CompilerType
Builder::CreateObjCInterfaceType(llvm::StringRef name,
                                 std::optional<uint64_t> byte_size) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateObjCInterfaceType(name, byte_size));
}

CompilerType Builder::CreateArrayType(CompilerType element_type,
                                      std::optional<uint64_t> num_elements) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateArrayType(ToTypeRef(element_type), num_elements));
}

CompilerType Builder::CreatePointerType(CompilerType pointee_type,
                                        bool is_block) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreatePointerType(ToTypeRef(pointee_type), is_block));
}

CompilerType Builder::CreateReferenceType(CompilerType pointee_type,
                                          bool is_rvalue) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateReferenceType(ToTypeRef(pointee_type), is_rvalue));
}

CompilerType Builder::CreateMemberPointerType(CompilerType pointee_type,
                                              CompilerType containing_type) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateMemberPointerType(
      ToTypeRef(pointee_type), ToTypeRef(containing_type)));
}

CompilerType Builder::CreateTypedefType(llvm::StringRef name,
                                        CompilerType underlying_type) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateTypedefType(name, ToTypeRef(underlying_type)));
}

CompilerType Builder::CreateCVQualifiedType(CompilerType underlying_type,
                                            bool is_const, bool is_volatile) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateCVQualifiedType(
      ToTypeRef(underlying_type), is_const, is_volatile));
}

CompilerType Builder::CreatePtrAuthType(CompilerType underlying_type,
                                        unsigned key, bool addr_discriminated,
                                        unsigned extra_discriminator) {
  return m_ts.GetCompilerType(m_ts.m_context.CreatePtrAuthType(
      ToTypeRef(underlying_type), key, addr_discriminated,
      extra_discriminator));
}

CompilerType Builder::CreateElaboratedType(llvm::StringRef spelling,
                                           CompilerType underlying_type) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateElaboratedType(
      spelling, ToTypeRef(underlying_type)));
}

CompilerType Builder::CreateEnumType(llvm::StringRef name,
                                     std::optional<uint64_t> byte_size,
                                     CompilerType underlying_type,
                                     bool is_scoped) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateEnumType(
      name, byte_size, ToTypeRef(underlying_type), is_scoped));
}

CompilerType Builder::CreateFunctionType(CompilerType return_type,
                                         bool is_variadic) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateFunctionType(
      ToTypeRef(return_type), is_variadic));
}

CompilerType Builder::CreateComplexType(CompilerType element_type) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateComplexType(ToTypeRef(element_type)));
}

void Builder::AddParameter(CompilerType function_type,
                           CompilerType param_type) {
  auto *func = llvm::dyn_cast_or_null<cpp_typesystem::FunctionType>(
      static_cast<cpp_typesystem::Type *>(function_type.GetOpaqueQualType()));
  if (func)
    m_ts.m_context.AddParameter(*func, ToTypeRef(param_type));
}

void Builder::AddMemberFunction(cpp_typesystem::RecordType &record,
                                llvm::StringRef name, CompilerType function_type,
                                llvm::StringRef asm_label,
                                llvm::StringRef mangled_name, bool is_static,
                                bool is_const, bool is_volatile,
                                bool is_virtual, RefQualifier ref_qualifier,
                                MemberFunctionKind kind) {
  cpp_typesystem::MemberFunction method;
  method.name = GetIdentifier(name);
  method.type = ToTypeRef(function_type);
  method.asm_label = GetIdentifier(asm_label);
  method.mangled_name = GetIdentifier(mangled_name);
  method.is_static = is_static;
  method.is_const = is_const;
  method.is_volatile = is_volatile;
  method.is_virtual = is_virtual;
  method.ref_qualifier = ref_qualifier;
  method.kind = kind;
  m_ts.m_context.AddMemberFunction(record, method);
}

void Builder::AddStaticDataMember(cpp_typesystem::RecordType &record,
                                  llvm::StringRef name, cpp_typesystem::Type *type,
                                  llvm::StringRef mangled_name,
                                  std::optional<uint64_t> const_value) {
  cpp_typesystem::StaticDataMember member;
  member.name = GetIdentifier(name);
  member.type = ToTypeRef(type);
  member.mangled_name = GetIdentifier(mangled_name);
  member.const_value = const_value;
  m_ts.m_context.AddStaticDataMember(record, member);
}

cpp_typesystem::Identifier Builder::GetIdentifier(llvm::StringRef name) {
  return m_ts.m_context.GetIdentifier(name);
}

void Builder::SetRecordComplete(cpp_typesystem::RecordType &record) {
  m_ts.m_context.SetComplete(record);
}

void Builder::SetRecordTemplateInstantiation(
    cpp_typesystem::RecordType &record) {
  m_ts.m_context.SetTemplateInstantiation(record);
}

void Builder::SetRecordAnonymousStructOrUnion(
    cpp_typesystem::RecordType &record) {
  m_ts.m_context.SetAnonymousStructOrUnion(record);
}

void Builder::SetRecordAnonymousStructOrUnion(
    cpp_typesystem::RecordType &record, const cpp_typesystem::RecordType &parent) {
  m_ts.m_context.SetAnonymousStructOrUnion(record, parent);
}

void Builder::SetRecordArgPassingKind(
    cpp_typesystem::RecordType &record,
    cpp_typesystem::RecordType::ArgPassingKind kind) {
  m_ts.m_context.SetArgPassingKind(record, kind);
}

void Builder::SetRecordMemberFunctionsParsed(
    cpp_typesystem::RecordType &record) {
  m_ts.m_context.SetMemberFunctionsParsed(record);
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
                           cpp_typesystem::Type *type, uint64_t byte_offset,
                           bool is_virtual,
                           std::optional<uint64_t> vbase_offset_offset) {
  m_ts.m_context.AddBaseClass(record, ToTypeRef(type), byte_offset, is_virtual,
                              vbase_offset_offset);
}

void Builder::SetRecordPolymorphic(cpp_typesystem::ClassType &record) {
  m_ts.m_context.SetPolymorphic(record);
}

void Builder::SetObjCSuperClass(cpp_typesystem::ObjCInterfaceType &record,
                                cpp_typesystem::Type *superclass) {
  m_ts.m_context.SetObjCSuperClass(record, ToTypeRef(superclass));
}

void Builder::AddObjCMethod(cpp_typesystem::ObjCInterfaceType &record,
                            llvm::StringRef name, CompilerType function_type,
                            llvm::StringRef asm_label, bool is_class_method,
                            bool is_variadic, bool is_direct) {
  cpp_typesystem::ObjCMethod method;
  method.name = GetIdentifier(name);
  method.type = ToTypeRef(function_type);
  method.asm_label = GetIdentifier(asm_label);
  method.is_class_method = is_class_method;
  method.is_variadic = is_variadic;
  method.is_direct = is_direct;
  m_ts.m_context.AddObjCMethod(record, std::move(method));
}

void Builder::AddEnumerator(cpp_typesystem::EnumType &enum_type,
                            cpp_typesystem::Identifier name, uint64_t value) {
  m_ts.m_context.AddEnumerator(enum_type, name, value);
}

void Builder::AddTemplateArgument(cpp_typesystem::RecordType &record,
                                  lldb::TemplateArgumentKind kind,
                                  cpp_typesystem::Type *type,
                                  uint64_t integral_value, bool is_default) {
  cpp_typesystem::TemplateArgument arg;
  arg.kind = kind;
  arg.type = ToTypeRef(type);
  arg.integral_value = integral_value;
  arg.is_default = is_default;
  m_ts.m_context.AddTemplateArgument(record, arg);
}

void Builder::AddTemplateTemplateArgument(cpp_typesystem::RecordType &record,
                                          llvm::StringRef name,
                                          bool is_default) {
  cpp_typesystem::TemplateArgument arg;
  arg.kind = lldb::eTemplateArgumentKindTemplate;
  arg.name = GetIdentifier(name);
  arg.is_default = is_default;
  m_ts.m_context.AddTemplateArgument(record, arg);
}

const cpp_typesystem::Namespace *
Builder::GetNamespace(llvm::StringRef name, const cpp_typesystem::Namespace *parent,
                      bool is_inline) {
  return m_ts.m_context.GetNamespace(GetIdentifier(name), parent, is_inline);
}

void Builder::SetDeclContext(CompilerType type,
                             const cpp_typesystem::Namespace *ns) {
  if (auto *t = static_cast<cpp_typesystem::Type *>(type.GetOpaqueQualType()))
    t->SetDeclContext(ns);
}

void Builder::SetUnqualifiedName(CompilerType type, llvm::StringRef name) {
  if (auto *t = static_cast<cpp_typesystem::Type *>(type.GetOpaqueQualType()))
    t->SetUnqualifiedName(GetIdentifier(name));
}

void Builder::AddNestedType(cpp_typesystem::RecordType &record,
                            cpp_typesystem::Identifier name,
                            cpp_typesystem::Type *type) {
  m_ts.m_context.AddNestedType(record, name, ToTypeRef(type));
}
