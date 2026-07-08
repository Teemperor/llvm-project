//===-- Records.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_RECORDS_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_RECORDS_H

#include <vector>

#include "Identifier.h"
#include "Type.h"

namespace lldb_private {
namespace cpp_typesystem {

class RecordField {
public:
private:
  TypeRef m_type;
  Identifier m_name;
};

// Represents a struct or a class in C/C++.
class Record {
public:
private:
  /// True if we completed the record.
  bool m_completed : 1;
  /// True if this is a forward declaration and we don't know the contents
  /// of this class.
  bool m_forward_decl : 1;
  std::vector<RecordField> m_members;
};

/// Represents an Objective-C class.
class ObjCClass {
public:
private:
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_RECORDS_H
