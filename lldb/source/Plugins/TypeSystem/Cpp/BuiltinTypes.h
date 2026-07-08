//===-- BuiltinTypes.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILTIN_TYPES_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILTIN_TYPES_H

#include "Type.h"

namespace lldb_private {
namespace cpp_typesystem {

/// This type represents builtin types like int/long/char/etc.
class BuiltinType : public Type {
public:
  void SetEncoding(lldb::Encoding encoding) { m_encoding = encoding; }
  lldb::Encoding GetEncoding() const override { return m_encoding; }

  lldb::Format GetFormat() const override {
    switch (m_encoding) {
    case lldb::eEncodingSint:
      return lldb::eFormatDecimal;
    case lldb::eEncodingUint:
      return lldb::eFormatUnsigned;
    case lldb::eEncodingIEEE754:
      return lldb::eFormatFloat;
    default:
      return lldb::eFormatDefault;
    }
  }

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassBuiltin;
  }

  uint32_t GetTypeInfo() const override {
    uint32_t info = lldb::eTypeIsBuiltIn | lldb::eTypeHasValue;
    switch (m_encoding) {
    case lldb::eEncodingSint:
      info |= lldb::eTypeIsScalar | lldb::eTypeIsInteger | lldb::eTypeIsSigned;
      break;
    case lldb::eEncodingUint:
      info |= lldb::eTypeIsScalar | lldb::eTypeIsInteger;
      break;
    case lldb::eEncodingIEEE754:
      info |= lldb::eTypeIsScalar | lldb::eTypeIsFloat | lldb::eTypeIsSigned;
      break;
    default:
      break;
    }
    return info;
  }

private:
  lldb::Encoding m_encoding = lldb::eEncodingInvalid;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILTIN_TYPES_H
