//===-- Context.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Context.h"

#include <cassert>

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

const Namespace *Context::GetNamespace(Identifier name, const Namespace *parent,
                                       bool is_inline) {
  // Deduplicate by (parent, interned-name storage, is_inline). Identifiers from
  // the same IdentifierMap share backing storage, so the StringRef's data
  // pointer is a stable key for a given name.
  auto key = std::make_tuple(parent, (const void *)name.GetName().data(),
                             is_inline);
  auto it = m_namespace_map.find(key);
  if (it != m_namespace_map.end())
    return it->second;

  // Namespace's constructor is private; Context is a friend, so construct it
  // directly here rather than via make_unique.
  m_namespaces.emplace_back(
      std::unique_ptr<Namespace>(new Namespace(name, parent, is_inline)));
  const Namespace *ns = m_namespaces.back().get();
  m_namespace_map.emplace(key, ns);
  return ns;
}

BuiltinType *Context::GetBuiltinType(llvm::StringRef name,
                                     std::optional<uint64_t> byte_size,
                                     lldb::Encoding encoding,
                                     lldb::Format format) {
  // Prefer the shared canonical instance when the attributes describe one of
  // the enumerated builtin types.
  if (BuiltinType *known = builtin_types.Match(name, encoding, byte_size))
    return known;

  // Otherwise fall back to a bespoke type owned by this Context.
  auto type = std::make_unique<BuiltinType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  type->SetEncoding(encoding);
  type->SetFormat(format);
  return Track(std::move(type));
}

RecordType *Context::CreateRecordType(llvm::StringRef name,
                                      std::optional<uint64_t> byte_size,
                                      bool is_cpp_class, bool is_union) {
  std::unique_ptr<RecordType> type;
  if (is_cpp_class)
    type = std::make_unique<ClassType>();
  else
    type = std::make_unique<StructType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  type->m_is_union = is_union;
  return Track(std::move(type));
}

ArrayType *Context::CreateArrayType(TypeRef element_type,
                                    std::optional<uint64_t> num_elements) {
  assert(element_type && "an array must have an element type");
  auto type = std::make_unique<ArrayType>();
  type->SetElementType(element_type);
  type->SetNumElements(num_elements);
  // The array's storage is the element size times the element count (when both
  // are known).
  if (num_elements)
    if (std::optional<uint64_t> elem_size = element_type.Get()->GetByteSize())
      type->SetByteSize(*elem_size * *num_elements);
  return Track(std::move(type));
}

PointerType *Context::CreatePointerType(TypeRef pointee_type) {
  auto type = std::make_unique<PointerType>();
  type->SetPointeeType(pointee_type);
  type->SetByteSize(m_opts.GetBuiltinSizes().pointer_size);
  return Track(std::move(type));
}

ReferenceType *Context::CreateReferenceType(TypeRef pointee_type,
                                            bool is_rvalue) {
  // Unlike a pointer (which can be `void *`), a reference always refers to a
  // concrete type.
  assert(pointee_type && "a reference must refer to a type");
  auto type = std::make_unique<ReferenceType>();
  type->SetPointeeType(pointee_type);
  type->SetIsRValue(is_rvalue);
  type->SetByteSize(m_opts.GetBuiltinSizes().pointer_size);
  return Track(std::move(type));
}

TypedefType *Context::CreateTypedefType(llvm::StringRef name,
                                        TypeRef underlying_type) {
  // A typedef always aliases a type. A `typedef void Foo;` is represented by
  // aliasing the `void` builtin, not by a null underlying type.
  assert(underlying_type && "a typedef must alias a type");
  auto type = std::make_unique<TypedefType>();
  type->SetName(GetIdentifier(name));
  type->SetUnderlyingType(underlying_type);
  // A typedef has the same storage as the type it aliases.
  type->SetByteSize(underlying_type.Get()->GetByteSize());
  return Track(std::move(type));
}

CVQualifiedType *Context::CreateCVQualifiedType(TypeRef underlying_type,
                                                bool is_const,
                                                bool is_volatile) {
  // A cv-qualified type always qualifies a type. `const/volatile void` (e.g.
  // the pointee of a `const void *`) qualifies the `void` builtin, not a null
  // underlying type.
  assert(underlying_type && "a cv-qualified type must qualify a type");
  auto type = std::make_unique<CVQualifiedType>();
  type->SetUnderlyingType(underlying_type);
  type->SetIsConst(is_const);
  type->SetIsVolatile(is_volatile);
  // A cv-qualified type has the same storage as its unqualified version.
  type->SetByteSize(underlying_type.Get()->GetByteSize());
  return Track(std::move(type));
}

EnumType *Context::CreateEnumType(llvm::StringRef name,
                                  std::optional<uint64_t> byte_size,
                                  TypeRef underlying_type, bool is_scoped) {
  auto type = std::make_unique<EnumType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  type->SetUnderlyingType(underlying_type);
  type->SetIsScoped(is_scoped);
  return Track(std::move(type));
}

FunctionType *Context::CreateFunctionType(TypeRef return_type,
                                          bool is_variadic) {
  auto type = std::make_unique<FunctionType>();
  type->SetReturnType(return_type);
  type->SetIsVariadic(is_variadic);
  return Track(std::move(type));
}
