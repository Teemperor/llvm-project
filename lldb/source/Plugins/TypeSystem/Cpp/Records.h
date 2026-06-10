//===-- Nodes.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_DECLS_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_DECLS_H

#include <vector>

namespace lldb_private {
namespace cpp_typesystem {

class CppContext;

class RecordField {
public:
private:
  TypeRef type;
};

// Represents a struct or a class in C/C++.
class Record {
public:
private:
    std::vector<RecordField> members;
};

/// Represents an Objective-C class.
class ObjCClass {
public:
private:
};

// Represents a C++ namespace.
class Namespace {
public:
private:
  std::vector<Namespace> m_nested;
  std::vector<Record> m_classes;
  std::vector<ObjCClass> m_classes;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CPPNODES_H
