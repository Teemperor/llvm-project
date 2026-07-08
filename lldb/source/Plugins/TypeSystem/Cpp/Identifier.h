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

class IdentifierMap;

/// Represents an unqualified name.
// E.g. `string` or `std`, but *NOT* `std::string`.
class Identifier {
public:
  Identifier() = default;

  llvm::StringRef GetName() const { return m_name; }

private:
  // Only IdentifierMap may build a non-empty Identifier. This guarantees the
  // backing storage is owned by (and outlives) that map, so an Identifier can
  // never dangle.
  friend class IdentifierMap;
  explicit Identifier(llvm::StringRef name) : m_name(name) {}

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

  Identifier getWithStaticStorageStr(llvm::StringRef name) {
    // We don't need to intern this.
    // TODO: We might later add some sanity checks here.
    return Identifier(name);
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
