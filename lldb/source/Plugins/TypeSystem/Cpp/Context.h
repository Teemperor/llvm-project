//===-- Context.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H

#include <memory>
#include <optional>
#include <vector>

#include "BuiltinTypes.h"
#include "LanguageOpts.h"
#include "Type.h"

namespace lldb_private {
namespace cpp_typesystem {

/// Owns all the Type nodes for a TypeSystemCpp and hands out stable pointers to
/// them. Types live as long as the Context (and therefore the TypeSystemCpp).
class Context {
public:
  explicit Context(const LanguageOpts &opts)
      : m_opts(opts), builtin_types(opts, identifiers) {}

  const LanguageOpts &GetLanguageOpts() const { return m_opts; }

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

  /// Create a record type. When \p is_cpp_class is true the record can carry
  /// C++-only information (base classes) and a ClassType is created; otherwise
  /// a plain StructType is used.
  RecordType *CreateRecordType(llvm::StringRef name,
                               std::optional<uint64_t> byte_size,
                               bool is_cpp_class);

  /// Create an array type of \p num_elements elements of \p element_type.
  /// \p num_elements is std::nullopt for an array of unknown bound.
  ArrayType *CreateArrayType(Type *element_type,
                             std::optional<uint64_t> num_elements);

  /// Intern a name into this Context's IdentifierMap. All Identifiers used by
  /// types owned by this Context must be created here so that their backing
  /// storage lives exactly as long as the Context (and its types).
  Identifier GetIdentifier(llvm::StringRef name) {
    return identifiers.get(name);
  }

private:
  template <typename T> T *Track(std::unique_ptr<T> type) {
    T *result = type.get();
    m_types.push_back(std::move(type));
    return result;
  }

  std::vector<std::unique_ptr<Type>> m_types;
  /// Target/language configuration (triple, builtin sizes, float semantics).
  LanguageOpts m_opts;
  IdentifierMap identifiers;
  /// This contains builtin types of C/C++/Objective-C such as
  /// int, long, etc.
  KnownBuiltinTypes builtin_types;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H
