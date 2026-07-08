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

/// This type represent builtin types like int/long/char/etc.
class BuiltinType : public Type {

};

class BuiltinTypes {
    // TODO: Add the other C types and set correct sizes.
    BuiltinType m_char_type;
    BuiltinType m_int_type;
    BuiltinType m_unsigned_int_type;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILTIN_TYPES_H
