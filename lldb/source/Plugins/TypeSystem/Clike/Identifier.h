//===-- Identifier.h --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_IDENTIFIER_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_IDENTIFIER_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Allocator.h"

#include <vector>

namespace lldb_private {
namespace clike_typesystem {

class IdentifierMap;

/// Represents an unqualified name.
/// E.g. `string` or `std`, but *NOT* `std::string`.
class Identifier {
public:
  Identifier() = default;

  llvm::StringRef GetName() const { return m_name; }

private:
  // Only IdentifierMap may build a non-empty Identifier. This guarantees the
  // backing storage is owned by (or outlives) that map, so an Identifier can
  // never dangle.
  friend class IdentifierMap;
  explicit Identifier(llvm::StringRef name) : m_name(name) {}

  // This is stored in an IdentifierMap.
  llvm::StringRef m_name;
};

/// Turns strings into unique IDs.
class IdentifierMap {
public:
  ~IdentifierMap();

  /// Returns an Identifier for \p name, copying the string into storage owned
  /// by this map.
  Identifier get(llvm::StringRef name);

  /// Like get(), but for strings whose backing storage is guaranteed to
  /// outlive this map (e.g. string literals). The string is *not* copied.
  Identifier getWithStaticStorageStr(llvm::StringRef name);

private:
  // Owns the copies made by get(). Freed after this map, so the StringRefs in
  // m_names that point into it stay valid for the map's whole lifetime.
  llvm::BumpPtrAllocator m_string_storage;
  // Every identifier handed out, uniqued by content. Entries either point into
  // m_string_storage (from get()) or into caller-owned static storage (from
  // getWithStaticStorageStr()).
  llvm::DenseSet<llvm::StringRef> m_names;
};

/// Represents a fully qualified name such as `std::string`.
class QualifiedName {
public:
private:
  std::vector<Identifier> m_identifiers;
};

}
}

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_IDENTIFIER_H
