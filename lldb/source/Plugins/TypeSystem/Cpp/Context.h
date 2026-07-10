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
  explicit Context(const LanguageOpts &opts) : builtin_types(opts) {
  }

  BuiltinType *CreateBuiltinType(llvm::StringRef name,
                                 std::optional<uint64_t> byte_size,
                                 lldb::Encoding encoding);
  StructType *CreateRecordType(llvm::StringRef name,
                               std::optional<uint64_t> byte_size);

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
  IdentifierMap identifiers;
  /// This contains builtin types of C/C++/Objective-C such as
  /// int, long, etc.
  KnownBuiltinTypes builtin_types;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H
