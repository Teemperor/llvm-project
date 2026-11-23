//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCOPEDTYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCOPEDTYPE_H

#include <utility>

#include "Identifier.h"
#include "NamedType.h"
#include "Type.h"

namespace lldb_private {
namespace clike {

/// Represents a named type that is declared in a scope (see DeclScope).
///
/// In addition to the unqualified name stored by NamedType, this stores the
/// qualified name of the type and the scope it is declared in.
template <typename Base> class ScopedType : public NamedType<Base> {
public:
  /// \p qualified_name The qualified name.
  /// \p scope The scope the type is declared in. Its unqualified name is the
  /// name of the NamedType.
  ///
  /// Any remaining arguments are forwarded to \p Base.
  template <typename... Args>
  ScopedType(Identifier qualified_name, DeclScope scope, Args &&...args)
      : NamedType<Base>(scope.GetUnqualifiedName(),
                        std::forward<Args>(args)...),
        m_qualified_name(qualified_name),
        m_decl_context(scope.GetDeclContext()) {}

  Identifier GetName() const override { return m_qualified_name; }
  const DeclarationContext *GetDeclContext() const override {
    return m_decl_context;
  }

private:
  Identifier m_qualified_name;
  const DeclarationContext *m_decl_context = nullptr;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCOPEDTYPE_H
