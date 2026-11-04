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

clike::Identifier Builder::GetIdentifier(llvm::StringRef name) {
  return m_ts.m_context.GetIdentifier(name);
}
