//===-- Identifier.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_IDENTIFIER_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_IDENTIFIER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/StringSet.h"

#include <vector>

namespace lldb_private {
namespace cpp_typesystem {

/// Represents an unqualified name.
// E.g. `string` or `std`, but *NOT* `std::string`.
class Identifier {
public:
  Identifier() = default;
  explicit Identifier(llvm::StringRef name) : m_name(name) {}

  llvm::StringRef GetName() const { return m_name; }

private:
  // This is stored in an IdentifierMap.
  llvm::StringRef m_name;
};

/// Turns strings into unique IDs.
class IdentifierMap {
public:
  Identifier get(llvm::StringRef name) {
    // Internalize the string so the returned Identifier refers to storage
    // owned by this map.
    return Identifier(m_names.insert(name).first->getKey());
  }

private:
  llvm::StringSet<> m_names;
};

/// Represents a fully qualified name such as `std::string`.
class QualifiedName {
public:
private:
  std::vector<Identifier> m_identifiers;
};

}
}

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_IDENTIFIER_H
