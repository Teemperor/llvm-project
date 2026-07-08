//===-- Function.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_FUNCTION_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_FUNCTION_H

#include <vector>

#include "Identifier.h"
#include "Type.h"

namespace lldb_private {
namespace cpp_typesystem {

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
  std::vector<FunctionArgument> m_args;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_FUNCTION_H
