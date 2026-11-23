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

/// Represents a named type that is declared in a scope (see
/// DeclarationContext).
///
/// In addition to the unqualified name stored by NamedType, this stores the
/// qualified name of the type and the scope it is declared in.
template <typename Base> class ScopedType : public NamedType<Base> {
public:
  /// \p qualified_name The qualified name.
  /// \p decl_context The scope the type is declared in.
  /// \p unqualified_name The name without the enclosing scopes, which is the
  /// name of the NamedType.
  ///
  /// Any remaining arguments are forwarded to \p Base.
  template <typename... Args>
  ScopedType(Identifier qualified_name, const DeclarationContext &decl_context,
             Identifier unqualified_name, Args &&...args)
      : NamedType<Base>(unqualified_name, std::forward<Args>(args)...),
        m_qualified_name(qualified_name), m_decl_context(decl_context) {}

  Identifier GetName() const override { return m_qualified_name; }
  const DeclarationContext &GetDeclContext() const override {
    return m_decl_context;
  }

private:
  Identifier m_qualified_name;
  const DeclarationContext &m_decl_context;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCOPEDTYPE_H
