//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_DECLARATIONCONTEXT_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_DECLARATIONCONTEXT_H

#include <cstdint>

namespace lldb_private {
namespace clike {

/// A scope that declarations (e.g. named types) can be declared in.
///
/// Declaration contexts form a chain from the innermost scope of a declaration
/// up to the global scope, which is represented by a null DeclarationContext.
/// For example, the chain of `Inner` in
///
/// \code
///   namespace ns { struct Outer { struct Inner {}; }; }
/// \endcode
///
/// is the record `Outer` followed by the namespace `ns`.
///
/// This uses LLVM-style RTTI (see GetKind), so a context can be checked and
/// cast to its concrete kind with llvm::isa/llvm::dyn_cast.
class DeclarationContext {
public:
  /// The kinds of scopes a declaration can be in.
  enum class Kind : uint8_t {
    /// A (possibly inline or anonymous) namespace.
    Namespace,
    /// A struct, class or union.
    Record,
    /// The body of a function (for function-local declarations).
    Function,
  };

  virtual ~DeclarationContext() = default;

  Kind GetKind() const { return m_kind; }

  /// The enclosing declaration context, or null if this context is declared in
  /// the global scope.
  virtual const DeclarationContext *GetParent() const = 0;

protected:
  explicit DeclarationContext(Kind kind) : m_kind(kind) {}

private:
  Kind m_kind;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_DECLARATIONCONTEXT_H
