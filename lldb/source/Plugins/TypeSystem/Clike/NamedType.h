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

/// Mixin that adds a name and declaration context to a type.
template <typename Base> class NamedType : public Base {
public:
  /// \p name is the qualified name. Any remaining arguments are forwarded to
  /// \p Base.
  template <typename... Args>
  NamedType(Identifier name, DeclScope scope, Args &&...args)
      : Base(std::forward<Args>(args)...), m_name(name),
        m_unqualified_name(scope.GetUnqualifiedName()),
        m_decl_context(scope.GetDeclContext()) {}

  Identifier GetName() const override { return m_name; }
  const Namespace *GetDeclContext() const override { return m_decl_context; }
  Identifier GetUnqualifiedName() const override { return m_unqualified_name; }

private:
  Identifier m_name;
  Identifier m_unqualified_name;
  const Namespace *m_decl_context = nullptr;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_NAMEDTYPE_H
