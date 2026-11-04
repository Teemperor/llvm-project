//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_CONTEXT_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_CONTEXT_H

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "BuiltinTypes.h"
#include "LanguageOpts.h"
#include "Type.h"
#include "TypeC.h"

#include "llvm/Support/Casting.h"

#include <map>
#include <tuple>

namespace lldb_private {
namespace clike {

/// Owns all the Type nodes for one TypeSystemClike instance.
class Context {
public:
  /// LanguageOpts is move-only, so the Context takes ownership of \p opts; it
  /// is initialized before the builtins because they are sized from it.
  explicit Context(LanguageOpts opts)
      : m_opts(std::move(opts)), builtin_types(m_opts, identifiers) {
    // The canonical builtins are created by KnownBuiltinTypes rather than
    // through Track, so claim ownership of them here.
    builtin_types.ForEachType(
        [this](Type *type) { type->SetOwningContext(*this); });
  }

  const LanguageOpts &GetLanguageOpts() const { return m_opts; }

  /// The target's pointer width in bytes (from the triple). Used to size
  /// pointer/reference types, which don't store a size of their own.
  uint64_t GetPointerSize() const {
    return m_opts.GetBuiltinSizes().pointer_size;
  }

  /// Returns a builtin type for the given attributes. When the attributes
  /// match one of the enumerated C/C++/Objective-C builtin types, the shared
  /// canonical instance is returned; otherwise a bespoke type is created and
  /// tracked by this Context.
  BuiltinType *GetBuiltinType(llvm::StringRef name,
                              std::optional<uint64_t> byte_size,
                              lldb::Encoding encoding, lldb::Format format);

  /// The canonical builtin type instance for a specific builtin kind (e.g.
  /// BuiltinKind::Int). Used to answer queries for "basic" types by kind.
  BuiltinType *GetBuiltinType(BuiltinKind kind) {
    return builtin_types.Get(kind);
  }

  /// The canonical builtin type whose spelling is \p name (e.g. "unsigned
  /// long"), or nullptr if no builtin is spelled that way. Used to answer type
  /// lookups by name.
  BuiltinType *GetBuiltinTypeByName(llvm::StringRef name) {
    return builtin_types.MatchByName(name);
  }

  /// Create a pointer type pointing to \p pointee_type. A `void *` points at
  /// the `void` builtin (see TypeRef), not at "nothing". Its byte size is the
  /// target's pointer size.
  PointerType *CreatePointerType(TypeRef pointee_type);

  /// Create a typedef named \p name aliasing \p underlying_type, declared in
  /// \p scope.
  TypedefType *CreateTypedefType(llvm::StringRef name, TypeRef underlying_type,
                                 DeclScope scope = {});

  /// Create a cv-qualified version of \p underlying_type.
  CVQualifiedType *CreateCVQualifiedType(TypeRef underlying_type, bool is_const,
                                         bool is_volatile);

  /// Intern a name into this Context's IdentifierMap. All Identifiers used by
  /// types owned by this Context must be created here so that their backing
  /// storage lives exactly as long as the Context (and its types).
  Identifier GetIdentifier(llvm::StringRef name) {
    return identifiers.get(name);
  }

private:
  template <typename T> T *Track(std::unique_ptr<T> type) {
    T *result = type.get();
    result->SetOwningContext(*this);
    m_types.push_back(std::move(type));
    return result;
  }

  std::vector<std::unique_ptr<Type>> m_types;
  /// Uniquing map for pointer types, keyed by (pointee type, is-block), so that
  /// two independently-formed `T *` are the same instance (needed for type
  /// equality) and a plain `T *` stays distinct from a block `T (^)`. See
  /// CreatePointerType / CreateBlockPointerType.
  std::map<std::pair<Type *, bool>, PointerType *> m_pointer_map;
  /// Target/language configuration (triple, builtin sizes, float semantics).
  LanguageOpts m_opts;
  IdentifierMap identifiers;
  /// This contains builtin types of C/C++/Objective-C such as
  /// int, long, etc.
  KnownBuiltinTypes builtin_types;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_CONTEXT_H
