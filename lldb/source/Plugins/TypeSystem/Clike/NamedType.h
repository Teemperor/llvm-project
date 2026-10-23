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

/// Represents a named type in a declaration context.
template <typename Base> class NamedType : public Base {
public:
  /// \p name The qualified name.
  ///
  /// Any remaining arguments are forwarded to \p Base.
  template <typename... Args>
  NamedType(Identifier name, DeclScope scope, Args &&...args)
      : Base(std::forward<Args>(args)...), m_name(name),
        m_unqualified_name(scope.GetUnqualifiedName()),
        m_decl_context(scope.GetDeclContext()),
        m_enclosing_namespace(scope.GetEnclosingNamespace()),
        m_unnamed_scope(scope.GetUnnamedScope()) {}

  Identifier GetName() const override { return m_name; }
  const Namespace *GetDeclContext() const override { return m_decl_context; }
  const Namespace *GetEnclosingNamespace() const override {
    return m_enclosing_namespace;
  }
  Identifier GetUnnamedScope() const override { return m_unnamed_scope; }
  Identifier GetUnqualifiedName() const override { return m_unqualified_name; }

  /// True if this type has no name at all, e.g. an unnamed
  /// `enum { ... } x;`.
  bool IsUnnamed() const {
    return m_name.GetName().empty() && m_unqualified_name.GetName().empty();
  }

private:
  Identifier m_name;
  Identifier m_unqualified_name;
  const Namespace *m_decl_context = nullptr;
  const Namespace *m_enclosing_namespace = nullptr;
  Identifier m_unnamed_scope;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMEDTYPE_H
