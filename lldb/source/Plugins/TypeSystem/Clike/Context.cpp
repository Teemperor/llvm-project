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
using namespace lldb_private::clike_typesystem;

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

const Decl *Context::GetOrCreateDecl(Decl::Payload payload) {
  if (auto it = m_decl_map.find(payload); it != m_decl_map.end())
    return it->second;
  m_decls.push_back(std::make_unique<Decl>(Decl{payload}));
  const Decl *result = m_decls.back().get();
  m_decl_map[payload] = result;
  return result;
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
                                      bool is_cpp_class, bool is_union,
                                      bool is_class_keyword) {
  std::unique_ptr<RecordType> type;
  if (is_cpp_class)
    type = std::make_unique<ClassType>();
  else
    type = std::make_unique<StructType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  type->m_is_union = is_union;
  type->m_is_class_keyword = is_class_keyword;
  return Track(std::move(type));
}

ObjCInterfaceType *
Context::CreateObjCInterfaceType(llvm::StringRef name,
                                 std::optional<uint64_t> byte_size) {
  auto type = std::make_unique<ObjCInterfaceType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  return Track(std::move(type));
}

ArrayType *Context::CreateArrayType(TypeRef element_type,
                                    std::optional<uint64_t> num_elements) {
  AssertOwnsRef(element_type);
  auto type = std::make_unique<ArrayType>(element_type);
  type->SetNumElements(num_elements);
  return Track(std::move(type));
}

PointerType *Context::CreatePointerType(TypeRef pointee_type) {
  // Unique pointer types by (pointee, is-block) so that two independently-formed
  // `T *` (e.g. a variable's type and `SBType::GetPointerType()`) are the same
  // instance and thus compare equal (SBType/CompilerType equality is identity of
  // the opaque type). This mirrors clang, whose ASTContext uniques pointer types.
  AssertOwnsRef(pointee_type);
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
  AssertOwnsRef(pointee_type);
  auto key = std::make_pair(&pointee_type.Get(), /*is_block=*/true);
  if (auto it = m_pointer_map.find(key); it != m_pointer_map.end())
    return llvm::cast<BlockPointerType>(it->second);
  BlockPointerType *result =
      Track(std::make_unique<BlockPointerType>(pointee_type));
  m_pointer_map[key] = result;
  return result;
}

ReferenceType *Context::CreateReferenceType(TypeRef pointee_type,
                                            bool is_rvalue) {
  AssertOwnsRef(pointee_type);
  auto type = std::make_unique<ReferenceType>(pointee_type);
  type->SetIsRValue(is_rvalue);
  type->SetByteSize(m_opts.GetBuiltinSizes().pointer_size);
  return Track(std::move(type));
}

MemberPointerType *Context::CreateMemberPointerType(TypeRef pointee_type,
                                                    TypeRef containing_type) {
  AssertOwnsRef(pointee_type);
  AssertOwnsRef(containing_type);
  auto type =
      std::make_unique<MemberPointerType>(pointee_type, containing_type);
  // Itanium ABI: a pointer to a non-static member function is two pointers
  // wide (the function pointer, or vtable-offset-tagged equivalent, plus the
  // `this` adjustment); a pointer to a non-static data member is one pointer
  // wide (the byte offset of the member).
  uint64_t pointer_size = m_opts.GetBuiltinSizes().pointer_size;
  type->SetByteSize(llvm::isa<FunctionType>(pointee_type.Get())
                        ? 2 * pointer_size
                        : pointer_size);
  return Track(std::move(type));
}

TypedefType *Context::CreateTypedefType(llvm::StringRef name,
                                        TypeRef underlying_type) {
  AssertOwnsRef(underlying_type);
  auto type = std::make_unique<TypedefType>(underlying_type);
  type->SetName(GetIdentifier(name));
  return Track(std::move(type));
}

CVQualifiedType *Context::CreateCVQualifiedType(TypeRef underlying_type,
                                                bool is_const,
                                                bool is_volatile) {
  AssertOwnsRef(underlying_type);
  auto type = std::make_unique<CVQualifiedType>(underlying_type);
  type->SetIsConst(is_const);
  type->SetIsVolatile(is_volatile);
  return Track(std::move(type));
}

PtrAuthType *Context::CreatePtrAuthType(TypeRef underlying_type, unsigned key,
                                        bool addr_discriminated,
                                        unsigned extra_discriminator) {
  AssertOwnsRef(underlying_type);
  auto type = std::make_unique<PtrAuthType>(underlying_type);
  type->SetKey(key);
  type->SetAddressDiscriminated(addr_discriminated);
  type->SetExtraDiscriminator(extra_discriminator);
  return Track(std::move(type));
}

ElaboratedType *Context::CreateElaboratedType(llvm::StringRef spelling,
                                              TypeRef underlying_type) {
  AssertOwnsRef(underlying_type);
  auto type = std::make_unique<ElaboratedType>(underlying_type);
  type->SetSpelling(GetIdentifier(spelling));
  return Track(std::move(type));
}

EnumType *Context::CreateEnumType(llvm::StringRef name,
                                  std::optional<uint64_t> byte_size,
                                  TypeRef underlying_type, bool is_scoped) {
  AssertOwnsRef(underlying_type);
  auto type = std::make_unique<EnumType>(underlying_type);
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  type->SetIsScoped(is_scoped);
  return Track(std::move(type));
}

FunctionType *Context::CreateFunctionType(TypeRef return_type,
                                          bool is_variadic,
                                          bool use_void_for_empty_params) {
  AssertOwnsRef(return_type);
  auto type = std::make_unique<FunctionType>(return_type);
  type->SetIsVariadic(is_variadic);
  type->SetUseVoidForEmptyParams(use_void_for_empty_params);
  return Track(std::move(type));
}

ComplexType *Context::CreateComplexType(TypeRef element_type) {
  AssertOwnsRef(element_type);
  return Track(std::make_unique<ComplexType>(element_type));
}

ForeignType *Context::GetForeignType(Context &owner, Type &type) {
  assert(&owner != this &&
         "a reference within a single Context needs no ForeignType");
  assert(owner.Owns(&type) &&
         "a foreign reference must name the Context that owns the type");
  // Interned by (owning Context, type) so that two references to the same
  // foreign type share one node -- both to avoid re-creating it and so that
  // type identity (which is node identity) is stable.
  auto key = std::make_pair(&owner, &type);
  if (auto it = m_foreign_type_map.find(key); it != m_foreign_type_map.end())
    return it->second;
  ForeignType *result = Track(std::make_unique<ForeignType>(owner, type));
  m_foreign_type_map[key] = result;
  return result;
}
