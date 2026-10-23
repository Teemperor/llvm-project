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
#include "Type.h"

namespace lldb_private {
namespace clike {

/// Stores the enclosing namespace of a type that can be declared inside one
/// (C++ classes, enums and typedefs).
///
/// Kept separate from NamedType so that named types which can never live in a
/// namespace (C structs, Objective-C interfaces) don't pay for the member;
/// they report the default null (global) DeclContext from Type.
///
/// The constructor has the same (name, scope, ...) shape as NamedType's and
/// forwards everything to \p Base, so it can wrap NamedType or any class
/// derived from it.
template <typename Base> class ScopedType : public Base {
public:
  template <typename... Args>
  ScopedType(Identifier name, DeclScope scope, Args &&...args)
      : Base(name, scope, std::forward<Args>(args)...),
        m_decl_context(scope.GetDeclContext()) {}

  const Namespace *GetDeclContext() const override { return m_decl_context; }

private:
  const Namespace *m_decl_context = nullptr;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SCOPEDTYPE_H
