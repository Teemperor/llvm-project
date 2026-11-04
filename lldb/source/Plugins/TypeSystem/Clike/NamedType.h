//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMEDTYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMEDTYPE_H

#include <utility>

#include "Identifier.h"
#include "Type.h"

namespace lldb_private {
namespace clike {

/// Represents a named type: its qualified and unqualified names.
///
/// The enclosing namespace is not stored here; see ScopedType for the kinds
/// that can be declared in a namespace. Only the unqualified name is taken from
/// \p scope.
template <typename Base> class NamedType : public Base {
public:
  /// \p name The qualified name.
  ///
  /// Any remaining arguments are forwarded to \p Base.
  template <typename... Args>
  NamedType(Identifier name, DeclScope scope, Args &&...args)
      : Base(std::forward<Args>(args)...), m_name(name),
        m_unqualified_name(scope.GetUnqualifiedName()) {}

  Identifier GetName() const override { return m_name; }
  Identifier GetUnqualifiedName() const override { return m_unqualified_name; }

  /// True if this type has no name at all, e.g. an unnamed
  /// `enum { ... } x;`.
  bool IsUnnamed() const {
    return m_name.GetName().empty() && m_unqualified_name.GetName().empty();
  }

private:
  Identifier m_name;
  Identifier m_unqualified_name;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMEDTYPE_H
