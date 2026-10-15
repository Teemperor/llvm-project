//===-- RecordType.h ------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_RECORDTYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_RECORDTYPE_H

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "ByteSizedType.h"
#include "Identifier.h"
#include "NamedType.h"
#include "Type.h"

namespace lldb_private {
namespace clike {

/// Common base class for structs, classes and unions.
///
/// Unlike other types, records can be modified after creation: they are often
/// created from a forward declaration and their members are added when they
/// are completed. Only Context can modify a record.
class RecordType
    : public llvm::RTTIExtends<RecordType, NamedType<ByteSizedType<Type>>> {
public:
  static char ID;

  explicit RecordType(Identifier name = Identifier(), DeclScope scope = {},
                      std::optional<uint64_t> byte_size = std::nullopt,
                      bool is_union = false, bool is_class_keyword = false)
      : RTTIExtends(name, scope, byte_size), m_complete(false),
        m_is_union(is_union), m_is_class_keyword(is_class_keyword),
        m_is_anonymous_struct_union(false), m_member_functions_parsed(false),
        m_arg_passing(ArgPassingKind::Unspecified) {}

  bool IsAggregate() const override { return true; }
  bool IsComplete() const override { return m_complete; }
  bool IsRecord() const override { return true; }
  lldb::LanguageType GetMinimumLanguage() const override {
    return lldb::eLanguageTypeC_plus_plus;
  }

  /// The alignment specified in the debug info (DW_AT_alignment), if any.
  std::optional<uint64_t> GetExplicitAlignmentInBits() const {
    return m_explicit_alignment_in_bits;
  }

  /// Returns the explicit alignment if there is one. Otherwise returns the
  /// largest alignment of the members, or std::nullopt for an incomplete
  /// record.
  std::optional<uint64_t> GetAlignmentInBits() const override {
    return ComputeAlignmentInBits(/*min_alignment_in_bits=*/8);
  }

  bool IsUnion() const override { return m_is_union; }

  /// True if this record was declared with the `class` keyword. Only used for
  /// naming unnamed records (e.g., `(unnamed class)`).
  bool IsClassKeyword() const override { return m_is_class_keyword; }

  /// True if this is an unnamed record that is also an unnamed member of its
  /// parent record (`struct { int y; };`), so its members are part of the
  /// parent's scope.
  bool IsAnonymousStructOrUnion() const override {
    return m_is_anonymous_struct_union;
  }

  /// The record that contains this anonymous struct or union, or null if this
  /// is not an anonymous struct or union. Used to name the record (e.g.,
  /// `MySock::(anonymous union)`).
  const RecordType *GetAnonymousParent() const { return m_anonymous_parent; }

  /// How this record is passed to and returned from functions, as specified
  /// by DW_AT_calling_convention.
  enum class ArgPassingKind : uint8_t {
    /// No calling convention was specified.
    Unspecified,
    /// DW_CC_pass_by_value
    PassByValue,
    /// DW_CC_pass_by_reference
    CannotPassInRegs,
  };
  ArgPassingKind GetArgPassingKind() const { return m_arg_passing; }

  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsStructUnion;
  }

  uint32_t GetNumFields() const override { return m_fields.size(); }
  const Field *GetFieldAtIndex(uint32_t idx) const override {
    if (idx < m_fields.size())
      return &m_fields[idx];
    return nullptr;
  }

  bool AreMemberFunctionsParsed() const override {
    return m_member_functions_parsed;
  }

  Type *GetNestedTypeWithName(llvm::StringRef name) const override {
    for (const auto &entry : m_nested_types)
      if (entry.first.GetName() == name)
        return &entry.second.Get();
    return nullptr;
  }

  uint32_t GetNumNestedTypes() const override { return m_nested_types.size(); }

  Type *GetHomogeneousAggregateBase(uint32_t &num_fields) const override;

protected:
  /// Computes the alignment of this record (see GetAlignmentInBits) assuming
  /// it is at least \p min_alignment_in_bits, e.g. because of members that
  /// aren't in the debug info.
  std::optional<uint64_t>
  ComputeAlignmentInBits(uint64_t min_alignment_in_bits) const;

private:
  // Only Context can modify records.
  friend class Context;
  void SetIsComplete(bool complete) { m_complete = complete; }
  void SetByteSizeFromDefinition(uint64_t byte_size) { SetByteSize(byte_size); }
  void SetExplicitAlignmentInBits(uint64_t align) {
    m_explicit_alignment_in_bits = align;
  }
  void SetIsAnonymousStructOrUnion(bool v) { m_is_anonymous_struct_union = v; }
  void SetAnonymousParent(const RecordType *parent) {
    m_anonymous_parent = parent;
  }
  void SetArgPassingKind(ArgPassingKind kind) { m_arg_passing = kind; }
  void SetMemberFunctionsParsed() { m_member_functions_parsed = true; }
  void AddField(Identifier name, TypeRef type, uint64_t byte_offset,
                uint32_t bitfield_bit_size = 0,
                uint32_t bitfield_bit_offset = 0) {
    m_fields.emplace_back(name, type, byte_offset, bitfield_bit_size,
                          bitfield_bit_offset);
  }
  void AddNestedType(Identifier name, TypeRef type) {
    m_nested_types.emplace_back(name, type);
  }

  // Larger members first so that the bit-fields fill the tail padding.
  const RecordType *m_anonymous_parent = nullptr;
  std::optional<uint64_t> m_explicit_alignment_in_bits;
  std::vector<Field> m_fields;
  std::vector<std::pair<Identifier, TypeRef>> m_nested_types;
  bool m_complete : 1;
  bool m_is_union : 1;
  bool m_is_class_keyword : 1;
  bool m_is_anonymous_struct_union : 1;
  bool m_member_functions_parsed : 1;
  ArgPassingKind m_arg_passing : 2;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_RECORDTYPE_H
