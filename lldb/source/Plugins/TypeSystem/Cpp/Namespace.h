//===-- Nodes.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_NAMESPACE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_NAMESPACE_H

#include <deque>

#include "Type.h"

namespace lldb_private {
namespace cpp_typesystem {

class CppContext;

// Represents a C++ namespace that holds various contents.
class Namespace {
public:
private:
  Identifier m_name;
  std::deque<Namespace> m_nested;
  std::deque<Record> m_classes;
  std::deque<ObjCClass> m_classes;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CPPNODES_H
