//===-- Type.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H

#include <cstdint>
#include <optional>
#include <vector>

#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "Identifier.h"

namespace lldb_private {
namespace cpp_typesystem {

class Context;
class Type;

/// A single data member of a record type.
struct Field {
  Identifier name;
  /// The type of this member. Owned by the same Context as the record.
  Type *type = nullptr;
  /// Offset of this member from the start of the record, in bytes.
  uint64_t byte_offset = 0;
};

/// A direct base class of a C++ class type.
struct BaseClass {
  /// The base class type. Owned by the same Context as the derived record.
  Type *type = nullptr;
  /// Offset of the base class subobject from the start of the derived record,
  /// in bytes.
  uint64_t byte_offset = 0;
};

/// References a type, potentially in another Context.
class TypeRef {
public:
private:
  Context *m_context = nullptr;
  Type *m_type = nullptr;
};

static_assert(sizeof(TypeRef) <= sizeof(void *) * 2,
              "TypeRef is expected to be a small reference class!");

/// Represents everything needed to understand a type.
///
/// A pointer to a Type is what TypeSystemCpp hands out as its
/// lldb::opaque_compiler_type_t, so the virtual functions below are the queries
/// that back the CompilerType API.
class Type : public llvm::RTTIExtends<Type, llvm::RTTIRoot> {
public:
  /// LLVM-style RTTI support (isa<>/cast<>/dyn_cast<>). Because Type already
  /// has a vtable, RTTIExtends implements this via a virtual dispatch keyed on
  /// the per-class `ID` address, so no per-object discriminator is needed.
  static char ID;

  virtual ~Type() = default;

  Identifier GetName() const { return m_name; }
  void SetName(Identifier name) { m_name = name; }

  std::optional<uint64_t> GetByteSize() const { return m_byte_size; }
  void SetByteSize(std::optional<uint64_t> byte_size) { m_byte_size = byte_size; }

  /// True if this type has members (a struct/class/union).
  virtual bool IsAggregate() const { return false; }

  /// True if all the information about this type is available. Non-aggregate
  /// types are always complete; records may start out as forward declarations.
  virtual bool IsComplete() const { return true; }

  virtual lldb::Encoding GetEncoding() const { return lldb::eEncodingInvalid; }
  virtual lldb::Format GetFormat() const { return lldb::eFormatDefault; }
  virtual lldb::TypeClass GetTypeClass() const { return lldb::eTypeClassOther; }
  virtual uint32_t GetTypeInfo() const { return 0; }

  /// Members of a record type. Empty for non-records.
  virtual uint32_t GetNumFields() const { return 0; }
  virtual const Field *GetFieldAtIndex(uint32_t idx) const { return nullptr; }

  /// Direct base classes of a C++ class type. Empty for everything else
  /// (including plain C structs, which never have base classes).
  virtual uint32_t GetNumBaseClasses() const { return 0; }
  virtual const BaseClass *GetBaseClassAtIndex(uint32_t idx) const {
    return nullptr;
  }

private:
  Identifier m_name;
  std::optional<uint64_t> m_byte_size;
};

/// Common base for C/C++ record types (struct/class/union). Owns the data
/// members and the forward-declaration/completion state that every record
/// shares. C++-only information (such as base classes) lives on ClassType, so
/// a plain StructType never reserves storage for it.
class RecordType : public llvm::RTTIExtends<RecordType, Type> {
public:
  static char ID;

  bool IsAggregate() const override { return true; }
  bool IsComplete() const override { return m_complete; }

  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsStructUnion;
  }

  uint32_t GetNumFields() const override { return m_fields.size(); }
  const Field *GetFieldAtIndex(uint32_t idx) const override {
    if (idx < m_fields.size())
      return &m_fields[idx];
    return nullptr;
  }

private:
  // Structural mutation happens after creation (during lazy completion, which
  // may run on worker threads), so it is gated: only Context can perform it,
  // and Context is only reachable through TypeSystemCpp's locked Builder.
  friend class Context;
  void SetIsComplete(bool complete) { m_complete = complete; }
  void AddField(Identifier name, Type *type, uint64_t byte_offset) {
    m_fields.push_back(Field{name, type, byte_offset});
  }

  bool m_complete = false;
  std::vector<Field> m_fields;
};

/// A C struct/union type. Records parsed from a C++ translation unit are
/// backed by ClassType instead, since only those can have base classes.
class StructType : public llvm::RTTIExtends<StructType, RecordType> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassStruct;
  }
};

/// A C++ class type. In addition to the data members every record has, it can
/// carry direct base classes.
class ClassType : public llvm::RTTIExtends<ClassType, RecordType> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassClass;
  }

  uint32_t GetNumBaseClasses() const override { return m_bases.size(); }
  const BaseClass *GetBaseClassAtIndex(uint32_t idx) const override {
    if (idx < m_bases.size())
      return &m_bases[idx];
    return nullptr;
  }

private:
  // Gated like RecordType's mutators (see there): only Context, reached through
  // the locked Builder, may add base classes.
  friend class Context;
  void AddBaseClass(Type *type, uint64_t byte_offset) {
    m_bases.push_back(BaseClass{type, byte_offset});
  }

  std::vector<BaseClass> m_bases;
};

/// A C array type: a fixed number of contiguous elements of the same type.
class ArrayType : public llvm::RTTIExtends<ArrayType, Type> {
public:
  static char ID;

  Type *GetElementType() const { return m_element_type; }
  void SetElementType(Type *type) { m_element_type = type; }

  /// Number of elements, or std::nullopt for an array of unknown bound.
  std::optional<uint64_t> GetNumElements() const { return m_num_elements; }
  void SetNumElements(std::optional<uint64_t> num_elements) {
    m_num_elements = num_elements;
  }

  // An array is an aggregate whose children are its elements.
  bool IsAggregate() const override { return true; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassArray;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsArray;
  }

private:
  Type *m_element_type = nullptr;
  std::optional<uint64_t> m_num_elements;
};

/// A simple pointer type.
class PointerType : public llvm::RTTIExtends<PointerType, Type> {
public:
  static char ID;

private:
  // The base type of this pointer.
  // E.g., for `int *` this is a ref to `int`.
  TypeRef m_base_type;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
