//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMESPACE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMESPACE_H

#include "DeclarationContext.h"
#include "Identifier.h"

namespace lldb_private {
namespace clike {

/// A C++ namespace that a type is declared in. Namespaces form a chain up to
/// the global namespace (a null parent). An inline namespace (e.g. libc++'s
/// `std::__1`) is transparent: it is skipped when building a type's qualified
/// name, so `std::__1::string` prints as `std::string`.
class Namespace : public DeclarationContext {
public:
  static bool classof(const DeclarationContext *ctx) {
    return ctx->GetKind() == Kind::Namespace;
  }

  Identifier GetName() const { return m_name; }
  /// The enclosing namespace, or null if this is a top-level namespace. A
  /// namespace can only be declared in another namespace.
  const Namespace *GetParent() const override { return m_parent; }
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
      : DeclarationContext(Kind::Namespace), m_name(name), m_parent(parent),
        m_is_inline(is_inline) {}

  Identifier m_name;
  const Namespace *m_parent = nullptr;
  bool m_is_inline = false;
};

/// The innermost namespace enclosing \p ctx (looking through any enclosing
/// records and functions), or \p ctx itself if it is a namespace. Null if
/// \p ctx is only enclosed by the global namespace.
const Namespace *GetEnclosingNamespace(const DeclarationContext *ctx);

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMESPACE_H
