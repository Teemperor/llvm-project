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
#include <optional>

namespace lldb_private {

class TypeSystemCpp;

namespace cpp_typesystem {

/// The mutating, non-thread-safe interface used to populate a TypeSystemCpp
/// (e.g. by the DWARF parser). It is only reachable through
/// TypeSystemCpp::Lock(), which hands one out inside a LockedPtr that owns
/// the type system's mutex. Because this is the only way to name these
/// methods, mutating the type system without serializing against other
/// threads is impossible by construction.
class Builder {
public:
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
                                bool is_cpp_class);
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
  void AddTemplateArgument(RecordType &record, TemplateArgument arg);
  void AddNestedType(RecordType &record, Identifier name, Type *type);
  void AddEnumerator(EnumType &enum_type, Identifier name, uint64_t value);

private:
  friend class lldb_private::TypeSystemCpp;
  explicit Builder(TypeSystemCpp &ts) : m_ts(ts) {}
  Builder(const Builder &) = delete;
  Builder &operator=(const Builder &) = delete;

  TypeSystemCpp &m_ts;
};

} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILDER_H
