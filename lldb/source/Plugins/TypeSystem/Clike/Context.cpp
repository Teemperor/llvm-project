//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Context.h"

#include <cassert>

using namespace lldb_private;
using namespace lldb_private::clike;

const Namespace *Context::GetNamespace(Identifier name, const Namespace *parent,
                                       bool is_inline) {
  // Deduplicate by (parent, interned-name storage, is_inline). Identifiers from
  // the same IdentifierMap share backing storage, so the StringRef's data
  // pointer is a stable key for a given name.
  auto key =
      std::make_tuple(parent, (const void *)name.GetName().data(), is_inline);
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
  return Track(std::make_unique<BuiltinType>(
      GetIdentifier(name), byte_size,
      builtin_types.GetAlignmentInBits(encoding, byte_size), encoding, format));
}

RecordType *Context::CreateRecordType(llvm::StringRef name,
                                      std::optional<uint64_t> byte_size,
                                      bool is_union, bool is_class_keyword,
                                      DeclScope scope) {
  return Track(std::make_unique<StructType>(
      GetIdentifier(name), scope, byte_size, is_union, is_class_keyword));
}

ArrayType *Context::CreateArrayType(TypeRef element_type,
                                    std::optional<uint64_t> num_elements,
                                    bool is_vector, uint64_t die_uid) {
  return Track(std::make_unique<ArrayType>(element_type, num_elements,
                                           is_vector, die_uid));
}

PointerType *Context::CreatePointerType(TypeRef pointee_type) {
  // Unique pointer types by (pointee, is-block) so that two
  // independently-formed `T *` (e.g. a variable's type and
  // `SBType::GetPointerType()`) are the same instance and thus compare equal
  // (SBType/CompilerType equality is identity of the opaque type). This mirrors
  // clang, whose ASTContext uniques pointer types.
  auto key = std::make_pair(&pointee_type.Get(), /*is_block=*/false);
  if (auto it = m_pointer_map.find(key); it != m_pointer_map.end())
    return it->second;
  PointerType *result = Track(std::make_unique<PointerType>(pointee_type));
  m_pointer_map[key] = result;
  return result;
}

BlockPointerType *Context::CreateBlockPointerType(TypeRef pointee_type) {
  // Uniqued like a plain pointer (see CreatePointerType), but the is-block bit
  // in the key keeps a block `T (^)` distinct from a plain `T *`.
  auto key = std::make_pair(&pointee_type.Get(), /*is_block=*/true);
  if (auto it = m_pointer_map.find(key); it != m_pointer_map.end())
    return llvm::cast<BlockPointerType>(it->second);
  BlockPointerType *result =
      Track(std::make_unique<BlockPointerType>(pointee_type));
  m_pointer_map[key] = result;
  return result;
}

TypedefType *Context::CreateTypedefType(llvm::StringRef name,
                                        TypeRef underlying_type,
                                        DeclScope scope) {
  return Track(std::make_unique<TypedefType>(GetIdentifier(name), scope,
                                             underlying_type));
}

CVQualifiedType *Context::CreateCVQualifiedType(TypeRef underlying_type,
                                                bool is_const,
                                                bool is_volatile) {
  return Track(std::make_unique<CVQualifiedType>(underlying_type, is_const,
                                                 is_volatile));
}

PtrAuthType *Context::CreatePtrAuthType(TypeRef underlying_type, unsigned key,
                                        bool addr_discriminated,
                                        unsigned extra_discriminator) {
  return Track(std::make_unique<PtrAuthType>(
      underlying_type,
      PtrAuthQualifier{key, addr_discriminated, extra_discriminator}));
}

ElaboratedType *Context::CreateElaboratedType(llvm::StringRef spelling,
                                              TypeRef underlying_type) {
  return Track(std::make_unique<ElaboratedType>(underlying_type,
                                                GetIdentifier(spelling)));
}

EnumType *Context::CreateEnumType(llvm::StringRef name,
                                  std::optional<uint64_t> byte_size,
                                  TypeRef underlying_type, bool is_scoped,
                                  std::vector<Enumerator> enumerators,
                                  DeclScope scope) {
  return Track(std::make_unique<EnumType>(GetIdentifier(name), scope, byte_size,
                                          underlying_type, is_scoped,
                                          std::move(enumerators)));
}

FunctionType *
Context::CreateFunctionType(TypeRef return_type, llvm::ArrayRef<TypeRef> params,
                            bool is_variadic, bool use_void_for_empty_params,
                            llvm::ArrayRef<Identifier> param_names) {
  return Track(std::make_unique<FunctionType>(
      return_type, std::vector<TypeRef>(params.begin(), params.end()),
      std::vector<Identifier>(param_names.begin(), param_names.end()),
      is_variadic, use_void_for_empty_params));
}

ComplexType *Context::CreateComplexType(TypeRef element_type) {
  return Track(std::make_unique<ComplexType>(element_type));
}
