//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BUILDER_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BUILDER_H

#include "Context.h"

#include "lldb/Symbol/CompilerType.h"
#include "lldb/lldb-enumerations.h"

#include <cstdint>
#include <optional>

namespace lldb_private {

class TypeSystemClike;

namespace clike {

/// The mutating interface used to populate a TypeSystemClike (e.g. by the DWARF
/// parser). This is the only way to reach the mutating API: a caller that wants
/// to change the type system must name a Builder.
///
/// Not thread safe, and it does not make itself so: every mutation path into a
/// TypeSystemClike today runs single-threaded, either because the caller
/// already holds the Module mutex (the DWARF parser) or because it is only
/// reached from the driver thread.
class Builder {
public:
  explicit Builder(TypeSystemClike &ts);

  Builder(const Builder &) = delete;
  Builder &operator=(const Builder &) = delete;

  // Type creation.
  CompilerType GetBuiltinType(llvm::StringRef name,
                              std::optional<uint64_t> byte_size,
                              lldb::Encoding encoding, lldb::Format format);
  /// The `void` builtin type. DWARF encodes `void` as the absence of a
  /// DW_AT_type, so the parser uses this to fill in the underlying type of a
  /// cv-qualified or typedef'd `void` (e.g. the pointee of a `const void *`).
  CompilerType GetVoidType();
  /// Create a pointer to \p pointee_type (an empty CompilerType denotes a
  /// `void *`).
  CompilerType CreatePointerType(CompilerType pointee_type);
  /// Create a typedef named \p name aliasing \p underlying_type, declared in
  /// \p scope.
  CompilerType CreateTypedefType(llvm::StringRef name,
                                 CompilerType underlying_type,
                                 DeclScope scope = {});
  /// Create a const- and/or volatile-qualified version of \p underlying_type.
  CompilerType CreateCVQualifiedType(CompilerType underlying_type,
                                     bool is_const, bool is_volatile);
  /// Create an enumeration type with the given \p enumerators (whose names
  /// must come from GetIdentifier), declared in \p scope.
  CompilerType CreateEnumType(llvm::StringRef name,
                              std::optional<uint64_t> byte_size,
                              CompilerType underlying_type, bool is_scoped,
                              std::vector<Enumerator> enumerators = {},
                              DeclScope scope = {});
  /// Intern a name into the Context so it can be used for a type or record
  /// member. All Identifiers must be created this way.
  Identifier GetIdentifier(llvm::StringRef name);

private:
  /// Wrap a CompilerType into a TypeRef naming the Type it refers to. An empty
  /// CompilerType, or one belonging to another kind of type system, has no node
  /// to name and so yields std::nullopt; the caller decides what that means for
  /// the reference it was about to store (usually: don't build the type at
  /// all).
  ///
  /// The type need not be one this Builder's Context owns -- a reference is
  /// free to name a type another Context created, since the node itself records
  /// who owns it (see Type::GetOwningContext).
  std::optional<TypeRef> ToTypeRef(const CompilerType &type);
  /// Wrap a Type (e.g. one the DWARF parser resolved) into a TypeRef. Null
  /// yields std::nullopt.
  std::optional<TypeRef> ToTypeRef(Type *type) const;
  /// ToTypeRef, falling back to the `void` builtin when there is no type to
  /// name. For the references DWARF spells as absent but C spells as `void`: a
  /// pointer with no DW_AT_type is `void *`, and a subroutine with none returns
  /// `void`. Modelling those as a reference to `void` (rather than as no
  /// reference) is what lets every consumer skip a null check -- and mirrors
  /// how sugar already wraps the `void` builtin for `const void` /
  /// `typedef void`.
  TypeRef ToTypeRefOrVoid(const CompilerType &type);

  TypeSystemClike &m_ts;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_BUILDER_H
