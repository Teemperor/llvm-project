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

const Decl *Context::GetOrCreateDecl(Decl::Kind kind, const void *payload) {
  auto key = std::make_pair(kind, payload);
  if (auto it = m_decl_map.find(key); it != m_decl_map.end())
    return it->second;
  m_decls.push_back(std::make_unique<Decl>(Decl{kind, payload}));
  const Decl *result = m_decls.back().get();
  m_decl_map[key] = result;
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
  assert(element_type && "an array must have an element type");
  auto type = std::make_unique<ArrayType>();
  type->SetElementType(element_type);
  type->SetNumElements(num_elements);
  return Track(std::move(type));
}

PointerType *Context::CreatePointerType(TypeRef pointee_type) {
  // Unique pointer types by (pointee, is-block) so that two independently-formed
  // `T *` (e.g. a variable's type and `SBType::GetPointerType()`) are the same
  // instance and thus compare equal (SBType/CompilerType equality is identity of
  // the opaque type). This mirrors clang, whose ASTContext uniques pointer types.
  auto key = std::make_pair(pointee_type.Get(), /*is_block=*/false);
  if (auto it = m_pointer_map.find(key); it != m_pointer_map.end())
    return it->second;
  auto type = std::make_unique<PointerType>();
  type->SetPointeeType(WithOwningContext(pointee_type));
  PointerType *result = Track(std::move(type));
  m_pointer_map[key] = result;
  return result;
}

BlockPointerType *Context::CreateBlockPointerType(TypeRef pointee_type) {
  // Uniqued like a plain pointer (see CreatePointerType), but the is-block bit
  // in the key keeps a block `T (^)` distinct from a plain `T *`.
  auto key = std::make_pair(pointee_type.Get(), /*is_block=*/true);
  if (auto it = m_pointer_map.find(key); it != m_pointer_map.end())
    return llvm::cast<BlockPointerType>(it->second);
  auto type = std::make_unique<BlockPointerType>();
  type->SetPointeeType(WithOwningContext(pointee_type));
  BlockPointerType *result = Track(std::move(type));
  m_pointer_map[key] = result;
  return result;
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

MemberPointerType *Context::CreateMemberPointerType(TypeRef pointee_type,
                                                    TypeRef containing_type) {
  assert(pointee_type && "a pointer-to-member must point to a type");
  assert(containing_type && "a pointer-to-member must have a containing type");
  auto type = std::make_unique<MemberPointerType>();
  type->SetPointeeType(pointee_type);
  type->SetContainingType(containing_type);
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
  // A typedef always aliases a type. A `typedef void Foo;` is represented by
  // aliasing the `void` builtin, not by a null underlying type.
  assert(underlying_type && "a typedef must alias a type");
  auto type = std::make_unique<TypedefType>();
  type->SetName(GetIdentifier(name));
  type->SetUnderlyingType(underlying_type);
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
  return Track(std::move(type));
}

PtrAuthType *Context::CreatePtrAuthType(TypeRef underlying_type, unsigned key,
                                        bool addr_discriminated,
                                        unsigned extra_discriminator) {
  // The pointer-auth qualifier always qualifies a pointer (or a typedef of
  // one), so it always has an underlying type.
  assert(underlying_type && "a __ptrauth type must qualify a type");
  auto type = std::make_unique<PtrAuthType>();
  type->SetUnderlyingType(underlying_type);
  type->SetKey(key);
  type->SetAddressDiscriminated(addr_discriminated);
  type->SetExtraDiscriminator(extra_discriminator);
  return Track(std::move(type));
}

ElaboratedType *Context::CreateElaboratedType(llvm::StringRef spelling,
                                              TypeRef underlying_type) {
  assert(underlying_type && "elaborated sugar must wrap a type");
  auto type = std::make_unique<ElaboratedType>();
  type->SetSpelling(GetIdentifier(spelling));
  type->SetUnderlyingType(underlying_type);
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
                                          bool is_variadic,
                                          bool use_void_for_empty_params) {
  auto type = std::make_unique<FunctionType>();
  type->SetReturnType(return_type);
  type->SetIsVariadic(is_variadic);
  type->SetUseVoidForEmptyParams(use_void_for_empty_params);
  return Track(std::move(type));
}

ComplexType *Context::CreateComplexType(TypeRef element_type) {
  auto type = std::make_unique<ComplexType>();
  type->SetElementType(element_type);
  return Track(std::move(type));
}
