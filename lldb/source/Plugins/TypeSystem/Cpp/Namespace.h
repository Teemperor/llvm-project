//===-- Namespace.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_NAMESPACE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_NAMESPACE_H

#include <deque>
#include <memory>

#include "Identifier.h"
#include "Type.h"

namespace lldb_private {
namespace cpp_typesystem {

// Represents a C++ namespace that holds various contents.
class Namespace {
public:
private:
  Identifier m_name;
  // Nested namespaces are held via unique_ptr because Namespace is an
  // incomplete type here and std::deque requires a complete element type.
  std::deque<std::unique_ptr<Namespace>> m_nested;
  std::deque<StructType> m_records;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_NAMESPACE_H
