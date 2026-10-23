//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemClike.h"

using namespace lldb_private;
using namespace lldb_private::clike;
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
      m_ts.m_context.GetBuiltinType(clike::BuiltinKind::Void));
}

CompilerType Builder::CreateRecordType(llvm::StringRef name,
                                       std::optional<uint64_t> byte_size,
                                       bool is_union, bool is_class_keyword,
                                       DeclScope scope) {
  return m_ts.GetCompilerType(m_ts.m_context.CreateRecordType(
      name, byte_size, is_union, is_class_keyword, scope));
}

CompilerType Builder::CreateObjCInterfaceType(llvm::StringRef name,
                                              std::optional<uint64_t> byte_size,
                                              DeclScope scope) {
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateObjCInterfaceType(name, byte_size, scope));
}

CompilerType Builder::CreateArrayType(CompilerType element_type,
                                      std::optional<uint64_t> num_elements,
                                      bool is_vector, uint64_t die_uid) {
  std::optional<TypeRef> element = ToTypeRef(element_type);
  if (!element)
    return CompilerType();
  return m_ts.GetCompilerType(m_ts.m_context.CreateArrayType(
      *element, num_elements, is_vector, die_uid));
}

CompilerType Builder::CreatePointerType(CompilerType pointee_type) {
  // No pointee means `void *` -- DWARF spells that as a DW_TAG_pointer_type
  // with no DW_AT_type.
  return m_ts.GetCompilerType(
      m_ts.m_context.CreatePointerType(ToTypeRefOrVoid(pointee_type)));
}

CompilerType Builder::CreateBlockPointerType(CompilerType pointee_type) {
  // A block pointer is a pointer, so it follows the same rule (see above); in
  // practice its pointee is always the block's function type.
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateBlockPointerType(ToTypeRefOrVoid(pointee_type)));
}

CompilerType Builder::CreateTypedefType(llvm::StringRef name,
                                        CompilerType underlying_type,
                                        DeclScope scope) {
  // A `typedef void Foo;` aliases the `void` builtin, so a typedef always has
  // an underlying type to name.
  std::optional<TypeRef> underlying = ToTypeRef(underlying_type);
  if (!underlying)
    return CompilerType();
  return m_ts.GetCompilerType(
      m_ts.m_context.CreateTypedefType(name, *underlying, scope));
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

CompilerType
Builder::CreateEnumType(llvm::StringRef name, std::optional<uint64_t> byte_size,
                        CompilerType underlying_type, bool is_scoped,
                        std::vector<Enumerator> enumerators, DeclScope scope) {
  // An enum always has an integer type backing it (see EnumType). C says that
  // type is `int` unless stated otherwise, so default to it here. The DWARF
  // parser normally picks something better first -- a signed integer of the
  // enum's recorded width -- and only lands here when there is no width to go
  // on, or no builtin of that width (see
  // DWARFASTParserClike::ParseEnum and GetBuiltinTypeForEncodingAndBitSize).
  std::optional<TypeRef> underlying = ToTypeRef(underlying_type);
  if (!underlying)
    underlying = TypeRef(*m_ts.m_context.GetBuiltinType(BuiltinKind::Int));
  return m_ts.GetCompilerType(m_ts.m_context.CreateEnumType(
      name, byte_size, *underlying, is_scoped, std::move(enumerators), scope));
}

CompilerType
Builder::CreateFunctionType(CompilerType return_type,
                            llvm::ArrayRef<CompilerType> param_types,
                            bool is_variadic, bool use_void_for_empty_params,
                            llvm::ArrayRef<llvm::StringRef> param_names) {
  std::vector<TypeRef> params;
  params.reserve(param_types.size());
  for (const CompilerType &param_type : param_types) {
    std::optional<TypeRef> param = ToTypeRef(param_type);
    if (!param)
      return CompilerType();
    params.push_back(*param);
  }
  std::vector<Identifier> names;
  names.reserve(param_names.size());
  for (llvm::StringRef name : param_names)
    names.push_back(GetIdentifier(name));
  // A function that returns nothing returns `void`; DWARF spells that as a
  // missing DW_AT_type.
  return m_ts.GetCompilerType(m_ts.m_context.CreateFunctionType(
      ToTypeRefOrVoid(return_type), params, is_variadic,
      use_void_for_empty_params, names));
}

CompilerType Builder::CreateComplexType(CompilerType element_type) {
  std::optional<TypeRef> element = ToTypeRef(element_type);
  if (!element)
    return CompilerType();
  return m_ts.GetCompilerType(m_ts.m_context.CreateComplexType(*element));
}

clike::Identifier Builder::GetIdentifier(llvm::StringRef name) {
  return m_ts.m_context.GetIdentifier(name);
}

void Builder::SetRecordComplete(clike::RecordType &record) {
  m_ts.m_context.SetComplete(record);
}

void Builder::SetRecordByteSize(clike::RecordType &record, uint64_t byte_size) {
  m_ts.m_context.SetByteSize(record, byte_size);
}

void Builder::SetRecordExplicitAlignmentInBits(clike::RecordType &record,
                                               uint64_t align) {
  m_ts.m_context.SetExplicitAlignmentInBits(record, align);
}

void Builder::SetRecordAnonymousStructOrUnion(clike::RecordType &record) {
  m_ts.m_context.SetAnonymousStructOrUnion(record);
}

void Builder::SetRecordAnonymousStructOrUnion(clike::RecordType &record,
                                              const clike::RecordType &parent) {
  m_ts.m_context.SetAnonymousStructOrUnion(record, parent);
}

void Builder::SetRecordArgPassingKind(clike::RecordType &record,
                                      clike::RecordType::ArgPassingKind kind) {
  m_ts.m_context.SetArgPassingKind(record, kind);
}

void Builder::SetRecordMemberFunctionsParsed(clike::RecordType &record) {
  m_ts.m_context.SetMemberFunctionsParsed(record);
}

void Builder::AddField(clike::RecordType &record, clike::Identifier name,
                       clike::Type *type, uint64_t byte_offset,
                       uint32_t bitfield_bit_size,
                       uint32_t bitfield_bit_offset) {
  if (std::optional<TypeRef> field_type = ToTypeRef(type))
    m_ts.m_context.AddField(record, name, *field_type, byte_offset,
                            bitfield_bit_size, bitfield_bit_offset);
}

void Builder::SetObjCSuperClass(clike::ObjCInterfaceType &record,
                                clike::Type *superclass) {
  if (std::optional<TypeRef> super = ToTypeRef(superclass))
    m_ts.m_context.SetObjCSuperClass(record, *super);
}

void Builder::AddObjCMethod(clike::ObjCInterfaceType &record,
                            llvm::StringRef name, CompilerType function_type,
                            llvm::StringRef asm_label, bool is_class_method,
                            bool is_variadic, bool is_direct,
                            bool returns_instancetype) {
  // A method with no modeled signature can't be sent, so drop it instead of
  // recording an untyped one.
  std::optional<TypeRef> type = ToTypeRef(function_type);
  if (!type)
    return;
  clike::ObjCMethod method(*type);
  method.name = GetIdentifier(name);
  method.asm_label = GetIdentifier(asm_label);
  method.is_class_method = is_class_method;
  method.is_variadic = is_variadic;
  method.is_direct = is_direct;
  method.returns_instancetype = returns_instancetype;
  m_ts.m_context.AddObjCMethod(record, std::move(method));
}

const clike::Namespace *Builder::GetNamespace(llvm::StringRef name,
                                              const clike::Namespace *parent,
                                              bool is_inline) {
  return m_ts.m_context.GetNamespace(GetIdentifier(name), parent, is_inline);
}

void Builder::AddNestedType(clike::RecordType &record, clike::Identifier name,
                            clike::Type *type) {
  if (std::optional<TypeRef> nested = ToTypeRef(type))
    m_ts.m_context.AddNestedType(record, name, *nested);
}
