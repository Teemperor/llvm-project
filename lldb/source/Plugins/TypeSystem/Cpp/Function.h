//===-- CppNodes.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CPPNODES_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CPPNODES_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"

#include "llvm/ADT/SmallVector.h"

#include <vector>

namespace lldb_private {

class CppContext;

namespace CppNodes {

class FunctionArgument {
public:
private:
    TypeRef m_type;
    Identifier m_name;
};

/// Represents a C/C++ function.
class Function {
public:
private:
  // TODO: Some attributes and things like calling convention are missing.
  Identifier m_name;
  std::vector<FunctionArg> m_args;
};

}

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CPPNODES_H
