//===-- Type.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H

#include <cassert>
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

/// References a type, potentially in another Context. Type nodes are owned by
/// their Context, so a bare `Type *` does not by itself say where a referenced
/// type lives; a TypeRef pairs the type pointer with its owning Context. Every
/// class in this file stores TypeRefs (never a bare `Type *`) to reference
/// other types, because a type may reference another type in a different
/// Context.
class TypeRef {
public:
  TypeRef() = default;
  TypeRef(Context &context, Type *type) : m_context(&context), m_type(type) {}

  /// The referenced type, or null for an empty reference (e.g. the pointee of
  /// a `void *`).
  Type *Get() const { return m_type; }
  /// The Context that owns the referenced type, or null for an empty reference.
  Context *GetContext() const { return m_context; }

  /// True when this refers to a type (false for an empty reference).
  explicit operator bool() const { return m_type != nullptr; }

private:
  Context *m_context = nullptr;
  Type *m_type = nullptr;
};

static_assert(sizeof(TypeRef) <= sizeof(void *) * 2,
              "TypeRef is expected to be a small reference class!");

/// A single data member of a record type.
struct Field {
  Identifier name;
  /// The type of this member.
  TypeRef type;
  /// Offset of this member (or, for a bitfield, of its storage unit) from the
  /// start of the record, in bytes.
  uint64_t byte_offset = 0;
  /// For a bitfield: its width in bits. Zero means this is not a bitfield.
  uint32_t bitfield_bit_size = 0;
  /// For a bitfield: the offset of its first bit within the storage unit at
  /// byte_offset.
  uint32_t bitfield_bit_offset = 0;

  bool IsBitfield() const { return bitfield_bit_size != 0; }
};

/// A direct base class of a C++ class type.
struct BaseClass {
  /// The base class type.
  TypeRef type;
  /// Offset of the base class subobject from the start of the derived record,
  /// in bytes.
  uint64_t byte_offset = 0;
};

/// A template argument of a class template instantiation (e.g. the `int` and
/// `std::allocator<int>` of `std::vector<int, std::allocator<int>>`). Data
/// formatters rely on these to recover, for instance, a container's element
/// type.
struct TemplateArgument {
  lldb::TemplateArgumentKind kind = lldb::eTemplateArgumentKindNull;
  /// Type argument: the argument's type. Integral argument: the value's type.
  TypeRef type;
  /// Integral argument: the raw value bits (interpret using `type`'s
  /// signedness/size).
  uint64_t integral_value = 0;
};

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

  /// Template arguments, if this record is a class-template instantiation.
  uint32_t GetNumTemplateArguments() const { return m_template_args.size(); }
  const TemplateArgument *GetTemplateArgumentAtIndex(uint32_t idx) const {
    if (idx < m_template_args.size())
      return &m_template_args[idx];
    return nullptr;
  }

  /// Look up a type declared directly inside this record (a nested typedef,
  /// class, union or enum) by its unqualified name. Returns null if there is no
  /// such nested type. Data formatters use this to reach a container's internal
  /// helper types (e.g. a tree's "__node_pointer").
  Type *GetNestedTypeWithName(llvm::StringRef name) const {
    for (const auto &entry : m_nested_types)
      if (entry.first.GetName() == name)
        return entry.second.Get();
    return nullptr;
  }

private:
  // Structural mutation happens after creation (during lazy completion, which
  // may run on worker threads), so it is gated: only Context can perform it,
  // and Context is only reachable through TypeSystemCpp's locked Builder.
  friend class Context;
  void SetIsComplete(bool complete) { m_complete = complete; }
  void AddField(Identifier name, TypeRef type, uint64_t byte_offset,
                uint32_t bitfield_bit_size = 0,
                uint32_t bitfield_bit_offset = 0) {
    Field f;
    f.name = name;
    f.type = type;
    f.byte_offset = byte_offset;
    f.bitfield_bit_size = bitfield_bit_size;
    f.bitfield_bit_offset = bitfield_bit_offset;
    m_fields.push_back(f);
  }
  void AddTemplateArgument(TemplateArgument arg) {
    m_template_args.push_back(arg);
  }
  void AddNestedType(Identifier name, TypeRef type) {
    m_nested_types.emplace_back(name, type);
  }

  bool m_complete = false;
  std::vector<Field> m_fields;
  std::vector<TemplateArgument> m_template_args;
  std::vector<std::pair<Identifier, TypeRef>> m_nested_types;
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
  void AddBaseClass(TypeRef type, uint64_t byte_offset) {
    m_bases.push_back(BaseClass{type, byte_offset});
  }

  std::vector<BaseClass> m_bases;
};

/// A C array type: a fixed number of contiguous elements of the same type.
class ArrayType : public llvm::RTTIExtends<ArrayType, Type> {
public:
  static char ID;

  Type *GetElementType() const { return m_element_type.Get(); }
  void SetElementType(TypeRef type) { m_element_type = type; }

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
  TypeRef m_element_type;
  std::optional<uint64_t> m_num_elements;
};

/// A simple pointer type.
class PointerType : public llvm::RTTIExtends<PointerType, Type> {
public:
  static char ID;

  /// The type this pointer points to. E.g., for `int *` this is `int`.
  /// May be null for `void *`.
  Type *GetPointeeType() const { return m_pointee_type.Get(); }
  void SetPointeeType(TypeRef type) { m_pointee_type = type; }

  lldb::Encoding GetEncoding() const override { return lldb::eEncodingUint; }
  lldb::Format GetFormat() const override { return lldb::eFormatHex; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassPointer;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsPointer | lldb::eTypeHasValue;
  }

private:
  TypeRef m_pointee_type;
};

/// A C++ reference type: lvalue `T &` or rvalue `T &&`. At runtime a reference
/// is represented by an address (like a pointer), but it is transparent when
/// exploring its value: its single child is the referenced object.
class ReferenceType : public llvm::RTTIExtends<ReferenceType, Type> {
public:
  static char ID;

  /// The type this reference refers to. E.g., for `int &` this is `int`.
  Type *GetPointeeType() const { return m_pointee_type.Get(); }
  void SetPointeeType(TypeRef type) { m_pointee_type = type; }

  /// True for an rvalue reference (`T &&`), false for an lvalue one (`T &`).
  bool IsRValue() const { return m_is_rvalue; }
  void SetIsRValue(bool is_rvalue) { m_is_rvalue = is_rvalue; }

  lldb::Encoding GetEncoding() const override { return lldb::eEncodingUint; }
  lldb::Format GetFormat() const override { return lldb::eFormatHex; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassReference;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsReference | lldb::eTypeHasValue;
  }

private:
  TypeRef m_pointee_type;
  bool m_is_rvalue = false;
};

/// Common base for "sugar" types that wrap another type and are transparent to
/// most layout/children queries: typedefs and cv-qualified types. Stripping all
/// sugar off a type yields its canonical type (see Type::GetCanonicalType-style
/// desugaring in TypeSystemCpp). The transparent virtual queries forward to the
/// underlying type; subclasses override the ones that must differ (e.g. a
/// typedef reports its own name and type class).
class SugarType : public llvm::RTTIExtends<SugarType, Type> {
public:
  static char ID;

  /// The immediately-wrapped type. Peel repeatedly to reach the canonical type.
  /// Never null: a `const void`/`typedef void` wraps the `void` builtin, so
  /// sugar always has a concrete underlying type (enforced by the Context
  /// factories that create these).
  Type *GetUnderlyingType() const { return m_underlying_type.Get(); }
  void SetUnderlyingType(TypeRef type) {
    assert(type && "sugar must wrap a type (use the void builtin for void)");
    m_underlying_type = type;
  }

  // Sugar is see-through: forward the value/layout queries to the wrapped type
  // so a `typedef`/`const` of an aggregate still looks like one.
  bool IsAggregate() const override {
    return m_underlying_type.Get()->IsAggregate();
  }
  bool IsComplete() const override {
    return m_underlying_type.Get()->IsComplete();
  }
  lldb::Encoding GetEncoding() const override {
    return m_underlying_type.Get()->GetEncoding();
  }
  lldb::Format GetFormat() const override {
    return m_underlying_type.Get()->GetFormat();
  }
  lldb::TypeClass GetTypeClass() const override {
    return m_underlying_type.Get()->GetTypeClass();
  }
  uint32_t GetTypeInfo() const override {
    return m_underlying_type.Get()->GetTypeInfo();
  }
  uint32_t GetNumFields() const override {
    return m_underlying_type.Get()->GetNumFields();
  }
  const Field *GetFieldAtIndex(uint32_t idx) const override {
    return m_underlying_type.Get()->GetFieldAtIndex(idx);
  }
  uint32_t GetNumBaseClasses() const override {
    return m_underlying_type.Get()->GetNumBaseClasses();
  }
  const BaseClass *GetBaseClassAtIndex(uint32_t idx) const override {
    return m_underlying_type.Get()->GetBaseClassAtIndex(idx);
  }

private:
  TypeRef m_underlying_type;
};

/// A typedef/using alias. It carries its own (alias) name but otherwise behaves
/// like the type it aliases.
class TypedefType : public llvm::RTTIExtends<TypedefType, SugarType> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassTypedef;
  }
  uint32_t GetTypeInfo() const override {
    return SugarType::GetTypeInfo() | lldb::eTypeIsTypedef;
  }
};

/// A `const`- and/or `volatile`-qualified type. Transparent like all sugar; it
/// only records which qualifiers apply so they can be reported and rendered in
/// the type name.
class CVQualifiedType : public llvm::RTTIExtends<CVQualifiedType, SugarType> {
public:
  static char ID;

  bool IsConst() const { return m_is_const; }
  void SetIsConst(bool is_const) { m_is_const = is_const; }
  bool IsVolatile() const { return m_is_volatile; }
  void SetIsVolatile(bool is_volatile) { m_is_volatile = is_volatile; }

private:
  bool m_is_const = false;
  bool m_is_volatile = false;
};

/// A single (name, value) constant of an enumeration.
struct Enumerator {
  Identifier name;
  /// The constant's value, stored as raw bits; interpret as signed when the
  /// enum's underlying type is signed.
  uint64_t value = 0;
};

/// A C/C++ enumeration type. It has an underlying integer type and a set of
/// named constants; scoped enums (`enum class`) are distinguished so their
/// enumerators are not treated as being in the enclosing scope.
class EnumType : public llvm::RTTIExtends<EnumType, Type> {
public:
  static char ID;

  Type *GetUnderlyingType() const { return m_underlying_type.Get(); }
  void SetUnderlyingType(TypeRef type) { m_underlying_type = type; }

  bool IsScoped() const { return m_is_scoped; }
  void SetIsScoped(bool is_scoped) { m_is_scoped = is_scoped; }

  /// True when the underlying integer type is signed.
  bool IsSigned() const {
    return !m_underlying_type ||
           m_underlying_type.Get()->GetEncoding() == lldb::eEncodingSint;
  }

  const std::vector<Enumerator> &GetEnumerators() const {
    return m_enumerators;
  }

  lldb::Encoding GetEncoding() const override {
    return m_underlying_type ? m_underlying_type.Get()->GetEncoding()
                             : lldb::eEncodingSint;
  }
  lldb::Format GetFormat() const override { return lldb::eFormatEnum; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassEnumeration;
  }
  uint32_t GetTypeInfo() const override {
    uint32_t info = lldb::eTypeIsEnumeration | lldb::eTypeHasValue |
                    lldb::eTypeIsScalar | lldb::eTypeIsInteger;
    if (IsSigned())
      info |= lldb::eTypeIsSigned;
    return info;
  }

private:
  // Gated like RecordType's mutators: only Context (through the locked Builder)
  // may add enumerators.
  friend class Context;
  void AddEnumerator(Identifier name, uint64_t value) {
    m_enumerators.push_back(Enumerator{name, value});
  }

  TypeRef m_underlying_type;
  bool m_is_scoped = false;
  std::vector<Enumerator> m_enumerators;
};
} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
