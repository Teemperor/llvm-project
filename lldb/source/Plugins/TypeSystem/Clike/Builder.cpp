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

CompilerType Builder::CreatePointerType(CompilerType pointee_type) {
  // No pointee means `void *` -- DWARF spells that as a DW_TAG_pointer_type
  // with no DW_AT_type.
  return m_ts.GetCompilerType(
      m_ts.m_context.CreatePointerType(ToTypeRefOrVoid(pointee_type)));
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

clike::Identifier Builder::GetIdentifier(llvm::StringRef name) {
  return m_ts.m_context.GetIdentifier(name);
}
