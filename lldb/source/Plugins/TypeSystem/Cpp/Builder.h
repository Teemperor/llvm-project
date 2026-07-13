//===-- Builder.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILDER_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILDER_H

#include "Context.h"

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/lldb-enumerations.h"

#include <cstdint>
#include <mutex>
#include <optional>

namespace lldb_private {

class TypeSystemCpp;

namespace cpp_typesystem {

/// The mutating, non-thread-safe interface used to populate a TypeSystemCpp
/// (e.g. by the DWARF parser). Constructing a Builder acquires the type
/// system's mutex and holds it for the Builder's lifetime, so every mutation
/// performed through it is serialized against other threads. This is the only
/// way to reach the mutating API: a caller that wants to change the type
/// system must name a Builder, and naming one takes the lock.
class Builder {
public:
  /// Acquire exclusive, serialized access to \p ts's mutable state. The lock
  /// is held until this Builder is destroyed.
  explicit Builder(TypeSystemCpp &ts);

  Builder(const Builder &) = delete;
  Builder &operator=(const Builder &) = delete;

  // Type creation.
  CompilerType GetBuiltinType(ConstString name,
                              std::optional<uint64_t> byte_size,
                              lldb::Encoding encoding, lldb::Format format);
  /// The `void` builtin type. DWARF encodes `void` as the absence of a
  /// DW_AT_type, so the parser uses this to fill in the underlying type of a
  /// cv-qualified or typedef'd `void` (e.g. the pointee of a `const void *`).
  CompilerType GetVoidType();
  CompilerType CreateRecordType(ConstString name,
                                std::optional<uint64_t> byte_size,
                                bool is_cpp_class, bool is_union = false);
  /// Create an array of \p num_elements elements of \p element_type (or an
  /// array of unknown bound when \p num_elements is std::nullopt).
  CompilerType CreateArrayType(CompilerType element_type,
                               std::optional<uint64_t> num_elements);
  /// Create a pointer to \p pointee_type (an empty CompilerType denotes a
  /// `void *`).
  CompilerType CreatePointerType(CompilerType pointee_type);
  /// Create an lvalue or rvalue reference to \p pointee_type.
  CompilerType CreateReferenceType(CompilerType pointee_type, bool is_rvalue);
  /// Create a typedef named \p name aliasing \p underlying_type.
  CompilerType CreateTypedefType(ConstString name,
                                 CompilerType underlying_type);
  /// Create a const- and/or volatile-qualified version of \p underlying_type.
  CompilerType CreateCVQualifiedType(CompilerType underlying_type,
                                     bool is_const, bool is_volatile);
  /// Create an enumeration type. Enumerators are added afterwards via
  /// AddEnumerator.
  CompilerType CreateEnumType(ConstString name,
                              std::optional<uint64_t> byte_size,
                              CompilerType underlying_type, bool is_scoped);
  /// Intern a name into the Context so it can be used for a type or record
  /// member. All Identifiers must be created this way.
  Identifier GetIdentifier(llvm::StringRef name);

  // Structural completion of a record type.
  void SetRecordComplete(RecordType &record);
  void AddField(RecordType &record, Identifier name, Type *type,
                uint64_t byte_offset, uint32_t bitfield_bit_size = 0,
                uint32_t bitfield_bit_offset = 0);
  void AddBaseClass(ClassType &record, Type *type, uint64_t byte_offset);
  void AddTemplateArgument(RecordType &record,
                           lldb::TemplateArgumentKind kind, Type *type,
                           uint64_t integral_value);
  void AddNestedType(RecordType &record, Identifier name, Type *type);
  void AddEnumerator(EnumType &enum_type, Identifier name, uint64_t value);

private:
  /// Wrap a CompilerType into a TypeRef that pairs the referenced Type with the
  /// Context that owns it (derived from the CompilerType's own type system, so
  /// the reference stays correct even when it points into another Context). An
  /// empty CompilerType (e.g. the `void *` pointee) yields an empty TypeRef.
  static TypeRef ToTypeRef(const CompilerType &type);
  /// Wrap a Type that this Builder's Context owns (e.g. one the DWARF parser
  /// resolved through this type system) into a TypeRef.
  TypeRef ToTypeRef(Type *type) const;

  TypeSystemCpp &m_ts;
  std::lock_guard<std::recursive_mutex> m_lock;
};

} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILDER_H
