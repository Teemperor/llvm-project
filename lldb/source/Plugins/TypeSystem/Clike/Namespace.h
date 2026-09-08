//===-- Namespace.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMESPACE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMESPACE_H

#include "Identifier.h"

namespace lldb_private {
namespace clike_typesystem {

/// A C++ namespace that a type is declared in. Namespaces form a chain up to
/// the global namespace (a null parent). An inline namespace (e.g. libc++'s
/// `std::__1`) is transparent: it is skipped when building a type's qualified
/// name, so `std::__1::string` prints as `std::string`.
class Namespace {
public:
  Identifier GetName() const { return m_name; }
  /// The enclosing namespace, or null if this is a top-level namespace.
  const Namespace *GetParent() const { return m_parent; }
  bool IsInline() const { return m_is_inline; }
  /// True for an unnamed namespace (`namespace { ... }`). Like clang, its
  /// `(anonymous namespace)` scope is elided from a type's display name, so a
  /// type declared directly in one prints unqualified (e.g. `Bar`, not
  /// `::Bar`). DWARF gives such a namespace no DW_AT_name, so it is interned
  /// with an empty name.
  bool IsAnonymous() const { return m_name.GetName().empty(); }

private:
  // Only Context creates and owns Namespaces (see Context::GetNamespace).
  friend class Context;
  Namespace(Identifier name, const Namespace *parent, bool is_inline)
      : m_name(name), m_parent(parent), m_is_inline(is_inline) {}

  Identifier m_name;
  const Namespace *m_parent = nullptr;
  bool m_is_inline = false;
};

} // namespace clike_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMESPACE_H
