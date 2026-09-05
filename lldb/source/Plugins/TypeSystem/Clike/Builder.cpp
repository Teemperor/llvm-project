//===-- Builder.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemClike.h"

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;
using namespace lldb;

Builder::Builder(TypeSystemClike &ts) : m_ts(ts) {}

std::optional<TypeRef> Builder::ToTypeRef(const CompilerType &type) {
  if (!type.GetTypeSystem().dyn_cast_or_null<TypeSystemClike>())
    return std::nullopt;
  return ToTypeRef(TypeSystemClike::GetClikeType(type.GetOpaqueQualType()));
}

std::optional<TypeRef> Builder::ToTypeRef(Type *type) const {
  if (!type)
    return std::nullopt;
  return TypeRef(*type);
}

TypeRef Builder::ToTypeRefOrVoid(const CompilerType &type) {
  if (std::optional<TypeRef> ref = ToTypeRef(type))
    return *ref;
  return TypeRef(*m_ts.m_context.GetBuiltinType(BuiltinKind::Void));
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
      m_ts.m_context.GetBuiltinType(clike_typesystem::BuiltinKind::Void));
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
  std::optional<TypeRef> element = ToTypeRef(element_type);
  if (!element)
    return CompilerType();
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateArrayType(*element, num_elements));
}

CompilerType Builder::CreatePointerType(CompilerType pointee_type) {
  // No pointee means `void *` -- DWARF spells that as a DW_TAG_pointer_type
  // with no DW_AT_type, and the ObjC encoding reader produces it for a pointer
  // to something it can't decode.
  return m_ts.GetCompilerType(
      m_ts.m_context.CreatePointerType(ToTypeRefOrVoid(pointee_type)));
}

CompilerType Builder::CreateBlockPointerType(CompilerType pointee_type) {
  // A block pointer is a pointer, so it follows the same rule (see above); in
  // practice its pointee is always the block's function type.
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateBlockPointerType(ToTypeRefOrVoid(pointee_type)));
}

CompilerType Builder::CreateReferenceType(CompilerType pointee_type,
                                          bool is_rvalue) {
  // Unlike a pointer, a reference always refers to a concrete type; there is
  // no `void &` to fall back on.
  std::optional<TypeRef> pointee = ToTypeRef(pointee_type);
  if (!pointee)
    return CompilerType();
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateReferenceType(*pointee, is_rvalue));
}

CompilerType Builder::CreateMemberPointerType(CompilerType pointee_type,
                                              CompilerType containing_type) {
  std::optional<TypeRef> pointee = ToTypeRef(pointee_type);
  std::optional<TypeRef> containing = ToTypeRef(containing_type);
  if (!pointee || !containing)
    return CompilerType();
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateMemberPointerType(*pointee, *containing));
}

CompilerType Builder::CreateTypedefType(llvm::StringRef name,
                                        CompilerType underlying_type) {
  // A `typedef void Foo;` aliases the `void` builtin, so a typedef always has
  // an underlying type to name.
  std::optional<TypeRef> underlying = ToTypeRef(underlying_type);
  if (!underlying)
    return CompilerType();
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateTypedefType(name, *underlying));
}

CompilerType Builder::CreateCVQualifiedType(CompilerType underlying_type,
                                            bool is_const, bool is_volatile) {
  // `const void` (the pointee of a `const void *`) qualifies the `void`
  // builtin, so there is always an underlying type here too.
  std::optional<TypeRef> underlying = ToTypeRef(underlying_type);
  if (!underlying)
    return CompilerType();
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateCVQualifiedType(*underlying, is_const, is_volatile));
}

CompilerType Builder::CreatePtrAuthType(CompilerType underlying_type,
                                        unsigned key, bool addr_discriminated,
                                        unsigned extra_discriminator) {
  std::optional<TypeRef> underlying = ToTypeRef(underlying_type);
  if (!underlying)
    return CompilerType();
  return m_ts.GetCompilerType(m_ts.m_context.CreatePtrAuthType(
      *underlying, key, addr_discriminated, extra_discriminator));
}

CompilerType Builder::CreateElaboratedType(llvm::StringRef spelling,
                                           CompilerType underlying_type) {
  std::optional<TypeRef> underlying = ToTypeRef(underlying_type);
  if (!underlying)
    return CompilerType();
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateElaboratedType(spelling, *underlying));
}

CompilerType Builder::CreateEnumType(llvm::StringRef name,
                                     std::optional<uint64_t> byte_size,
                                     CompilerType underlying_type,
                                     bool is_scoped) {
  // An enum always has an integer type backing it (see EnumType). C says that
  // type is `int` unless stated otherwise, so default to it here. The DWARF
  // parser normally picks something better first -- a signed integer of the
  // enum's recorded width -- and only lands here when there is no width to go
  // on, or no builtin of that width (see
  // DWARFASTParserClike::ParseEnum and GetBuiltinTypeForEncodingAndBitSize).
  std::optional<TypeRef> underlying = ToTypeRef(underlying_type);
  if (!underlying)
    underlying = TypeRef(*m_ts.m_context.GetBuiltinType(BuiltinKind::Int));
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateEnumType(name, byte_size, *underlying, is_scoped));
}

CompilerType Builder::CreateFunctionType(CompilerType return_type,
                                         bool is_variadic,
                                         bool use_void_for_empty_params) {
  // A function that returns nothing returns `void`; DWARF spells that as a
  // missing DW_AT_type.
  return m_ts.GetCompilerType(m_ts.m_context.CreateFunctionType(
      ToTypeRefOrVoid(return_type), is_variadic, use_void_for_empty_params));
}

CompilerType Builder::CreateComplexType(CompilerType element_type) {
  std::optional<TypeRef> element = ToTypeRef(element_type);
  if (!element)
    return CompilerType();
  return m_ts.GetCompilerType(m_ts.m_context.CreateComplexType(*element));
}

bool Builder::AddParameter(CompilerType function_type,
                           CompilerType param_type, llvm::StringRef name) {
  auto *func = llvm::dyn_cast_or_null<clike_typesystem::FunctionType>(
      static_cast<clike_typesystem::Type *>(function_type.GetOpaqueQualType()));
  if (!func)
    return false;
  std::optional<TypeRef> param = ToTypeRef(param_type);
  if (!param)
    return false;
  m_ts.m_context.AddParameter(*func, *param, GetIdentifier(name));
  return true;
}

void Builder::AddMemberFunction(clike_typesystem::ClassType &record,
                                llvm::StringRef name, CompilerType function_type,
                                llvm::StringRef asm_label,
                                llvm::StringRef mangled_name, bool is_static,
                                bool is_const, bool is_volatile,
                                bool is_virtual, RefQualifier ref_qualifier,
                                MemberFunctionKind kind) {
  // A method whose signature couldn't be modeled can't be called or displayed,
  // so drop it rather than record one with no type.
  std::optional<TypeRef> type = ToTypeRef(function_type);
  if (!type)
    return;
  clike_typesystem::MemberFunction method(*type);
  method.name = GetIdentifier(name);
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

void Builder::AddStaticDataMember(clike_typesystem::ClassType &record,
                                  llvm::StringRef name, clike_typesystem::Type *type,
                                  llvm::StringRef mangled_name,
                                  std::optional<uint64_t> const_value) {
  std::optional<TypeRef> member_type = ToTypeRef(type);
  if (!member_type)
    return;
  m_ts.m_context.AddStaticDataMember(
      record, clike_typesystem::StaticDataMember{
                  GetIdentifier(name), *member_type,
                  GetIdentifier(mangled_name), const_value});
}

clike_typesystem::Identifier Builder::GetIdentifier(llvm::StringRef name) {
  return m_ts.m_context.GetIdentifier(name);
}

void Builder::SetRecordComplete(clike_typesystem::RecordType &record) {
  m_ts.m_context.SetComplete(record);
}

void Builder::SetRecordTemplateInstantiation(
    clike_typesystem::ClassType &record) {
  m_ts.m_context.SetTemplateInstantiation(record);
}

void Builder::SetRecordAnonymousStructOrUnion(
    clike_typesystem::RecordType &record) {
  m_ts.m_context.SetAnonymousStructOrUnion(record);
}

void Builder::SetRecordAnonymousStructOrUnion(
    clike_typesystem::RecordType &record, const clike_typesystem::RecordType &parent) {
  m_ts.m_context.SetAnonymousStructOrUnion(record, parent);
}

void Builder::SetRecordArgPassingKind(
    clike_typesystem::RecordType &record,
    clike_typesystem::RecordType::ArgPassingKind kind) {
  m_ts.m_context.SetArgPassingKind(record, kind);
}

void Builder::SetRecordMemberFunctionsParsed(
    clike_typesystem::RecordType &record) {
  m_ts.m_context.SetMemberFunctionsParsed(record);
}

void Builder::AddField(clike_typesystem::RecordType &record,
                       clike_typesystem::Identifier name,
                       clike_typesystem::Type *type, uint64_t byte_offset,
                       uint32_t bitfield_bit_size,
                       uint32_t bitfield_bit_offset) {
  if (std::optional<TypeRef> field_type = ToTypeRef(type))
    m_ts.m_context.AddField(record, name, *field_type, byte_offset,
                            bitfield_bit_size, bitfield_bit_offset);
}

void Builder::AddBaseClass(clike_typesystem::ClassType &record,
                           clike_typesystem::Type *type, uint64_t byte_offset,
                           bool is_virtual,
                           std::optional<uint64_t> vbase_offset_offset) {
  if (std::optional<TypeRef> base = ToTypeRef(type))
    m_ts.m_context.AddBaseClass(record, *base, byte_offset, is_virtual,
                                vbase_offset_offset);
}

void Builder::SetRecordPolymorphic(clike_typesystem::ClassType &record) {
  m_ts.m_context.SetPolymorphic(record);
}

void Builder::SetObjCSuperClass(clike_typesystem::ObjCInterfaceType &record,
                                clike_typesystem::Type *superclass) {
  if (std::optional<TypeRef> super = ToTypeRef(superclass))
    m_ts.m_context.SetObjCSuperClass(record, *super);
}

void Builder::AddObjCMethod(clike_typesystem::ObjCInterfaceType &record,
                            llvm::StringRef name, CompilerType function_type,
                            llvm::StringRef asm_label, bool is_class_method,
                            bool is_variadic, bool is_direct,
                            bool returns_instancetype) {
  // As for a C++ member function: a method with no modeled signature can't be
  // sent, so drop it instead of recording an untyped one.
  std::optional<TypeRef> type = ToTypeRef(function_type);
  if (!type)
    return;
  clike_typesystem::ObjCMethod method(*type);
  method.name = GetIdentifier(name);
  method.asm_label = GetIdentifier(asm_label);
  method.is_class_method = is_class_method;
  method.is_variadic = is_variadic;
  method.is_direct = is_direct;
  method.returns_instancetype = returns_instancetype;
  m_ts.m_context.AddObjCMethod(record, std::move(method));
}

void Builder::AddEnumerator(clike_typesystem::EnumType &enum_type,
                            clike_typesystem::Identifier name, uint64_t value) {
  m_ts.m_context.AddEnumerator(enum_type, name, value);
}

void Builder::AddTemplateArgument(clike_typesystem::ClassType &record,
                                  lldb::TemplateArgumentKind kind,
                                  clike_typesystem::Type *type,
                                  uint64_t integral_value, bool is_default) {
  clike_typesystem::TemplateArgument arg;
  arg.kind = kind;
  arg.type = ToTypeRef(type);
  // DWARF encodes a `void` type argument (e.g. `coroutine_handle<void>`) by
  // omitting DW_AT_type on the DW_TAG_template_type_parameter DIE. Spell it as
  // the `void` builtin, so that "no type" stays reserved for the one argument
  // kind that really has none (a template-template argument).
  if (!arg.type && kind == lldb::eTemplateArgumentKindType)
    arg.type = TypeRef(*m_ts.m_context.GetBuiltinType(BuiltinKind::Void));
  arg.integral_value = integral_value;
  arg.is_default = is_default;
  m_ts.m_context.AddTemplateArgument(record, arg);
}

void Builder::AddTemplateTemplateArgument(clike_typesystem::ClassType &record,
                                          llvm::StringRef name,
                                          bool is_default) {
  clike_typesystem::TemplateArgument arg;
  arg.kind = lldb::eTemplateArgumentKindTemplate;
  arg.name = GetIdentifier(name);
  arg.is_default = is_default;
  m_ts.m_context.AddTemplateArgument(record, arg);
}

const clike_typesystem::Namespace *
Builder::GetNamespace(llvm::StringRef name, const clike_typesystem::Namespace *parent,
                      bool is_inline) {
  return m_ts.m_context.GetNamespace(GetIdentifier(name), parent, is_inline);
}

void Builder::SetDeclContext(CompilerType type,
                             const clike_typesystem::Namespace *ns) {
  if (auto *t = static_cast<clike_typesystem::Type *>(type.GetOpaqueQualType()))
    t->SetDeclContext(ns);
}

void Builder::SetUnqualifiedName(CompilerType type, llvm::StringRef name) {
  if (auto *t = static_cast<clike_typesystem::Type *>(type.GetOpaqueQualType()))
    t->SetUnqualifiedName(GetIdentifier(name));
}

void Builder::AddNestedType(clike_typesystem::RecordType &record,
                            clike_typesystem::Identifier name,
                            clike_typesystem::Type *type) {
  if (std::optional<TypeRef> nested = ToTypeRef(type))
    m_ts.m_context.AddNestedType(record, name, *nested);
}
