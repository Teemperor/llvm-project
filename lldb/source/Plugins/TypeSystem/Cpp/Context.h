//===-- CppContext.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"

#include "BuiltinTypes.h"

namespace lldb_private {
namespace cpp_typesystem {

/// Holds declaration and function nodes.
/// Also gives meaning to types.
class Context {

private:
    Namespace m_global_namespace;
    BuiltinTypes m_builtin;
    llvm::Triple m_triple;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H
