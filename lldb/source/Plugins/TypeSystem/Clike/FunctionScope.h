//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_FUNCTIONSCOPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_FUNCTIONSCOPE_H

#include "DeclarationContext.h"
#include "Identifier.h"

namespace lldb_private {
namespace clike {

/// The body of a function, as the declaration context of the types declared
/// in it (e.g. `Local` in `void func() { struct Local {}; }`).
class FunctionScope : public DeclarationContext {
public:
  static bool classof(const DeclarationContext *ctx) {
    return ctx->GetKind() == Kind::Function;
  }

  /// The spelling of the function as a scope of a type declared in it: its
  /// name followed by its parameter types (e.g. `func(int)`).
  Identifier GetName() const { return m_name; }

  /// The scope the function is declared in (e.g. the record for a member
  /// function), or null for the global namespace.
  const DeclarationContext *GetParent() const override { return m_parent; }

private:
  // Only Context creates and owns FunctionScopes (see
  // Context::GetFunctionScope).
  friend class Context;
  FunctionScope(Identifier name, const DeclarationContext *parent)
      : DeclarationContext(Kind::Function), m_name(name), m_parent(parent) {}

  Identifier m_name;
  const DeclarationContext *m_parent = nullptr;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_FUNCTIONSCOPE_H
