//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The type kinds C already has (and C++/Objective-C inherit unchanged): plain
// records, arrays, pointers, enums, functions, `_Complex`, and the sugar
// wrappers. See Type.h for the language-neutral core and TypeCpp.h /
// TypeObjC.h for the kinds specific to those languages.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_C_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_C_H

#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "ByteSizedType.h"
#include "Identifier.h"
#include "NamedType.h"
#include "RecordType.h"
#include "SugarType.h"
#include "Type.h"

namespace lldb_private {
namespace clike {

/// A C struct/union type.
class StructType : public llvm::RTTIExtends<StructType, RecordType> {
public:
  using RTTIExtends::RTTIExtends;

  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassStruct;
  }
};

/// A C array type with a fixed number of elements such a `int[4]`.
class ArrayType : public llvm::RTTIExtends<ArrayType, Type> {
public:
  static char ID;

  /// \p num_elements is std::nullopt for an array of unknown bound.
  /// \p is_vector marks a GCC/Clang vector type and \p die_uid is the UID of
  /// the DWARF DIE that produced the array (see IsVector and GetDIEUID).
  ArrayType(TypeRef element_type, std::optional<uint64_t> num_elements,
            bool is_vector = false, uint64_t die_uid = UINT64_MAX)
      : m_element_type(element_type),
        m_num_elements(num_elements.value_or(kNoNumElements)),
        m_die_uid(die_uid), m_is_vector(is_vector) {}

  Type *GetElementType() const { return &m_element_type.Get(); }
  Type *GetArrayElementType() const override { return GetElementType(); }

  /// Number of elements, or std::nullopt for an array of unknown bound.
  std::optional<uint64_t> GetNumElements() const override {
    return m_num_elements == kNoNumElements
               ? std::nullopt
               : std::optional<uint64_t>(m_num_elements);
  }

  /// The UID of the DWARF DIE that produced this array, or UINT64_MAX (i.e.
  /// LLDB_INVALID_UID). Lets the symbol file re-resolve a runtime
  /// (variable-length) bound for an otherwise-unbounded array. Stored as a
  /// plain uint64_t to avoid pulling lldb-forward.h (and its
  /// `lldb_private::Type` forward declaration, which would make `Type`
  /// ambiguous) into this header.
  uint64_t GetDIEUID() const override { return m_die_uid; }

  /// Whether this array is really a GCC/Clang vector type (DW_AT_GNU_vector,
  /// e.g. `float __attribute__((ext_vector_type(4)))`). Vectors are laid out
  /// like arrays but LLDB treats them as vector types for formatting.
  bool IsVector() const override { return m_is_vector; }

  // An array is an aggregate whose children are its elements.
  bool IsAggregate() const override { return true; }

  std::optional<uint64_t> GetByteSize() const override {
    std::optional<uint64_t> num_elements = GetNumElements();
    if (!num_elements)
      return std::nullopt;
    if (std::optional<uint64_t> elem_size = m_element_type.Get().GetByteSize())
      return *elem_size * *num_elements;
    return std::nullopt;
  }

  std::optional<uint64_t> GetAlignmentInBits() const override;

  lldb::TypeClass GetTypeClass() const override {
    return m_is_vector ? lldb::eTypeClassVector : lldb::eTypeClassArray;
  }

  uint32_t GetTypeInfo() const override {
    if (m_is_vector)
      return lldb::eTypeHasChildren | lldb::eTypeIsVector;
    return lldb::eTypeHasChildren | lldb::eTypeIsArray;
  }

private:
  // An array can't really have 2^64-1 elements, so steal that value as the
  // "unbounded" sentinel instead of paying for std::optional's separate bool.
  static constexpr uint64_t kNoNumElements = UINT64_MAX;
  TypeRef m_element_type;
  uint64_t m_num_elements;
  uint64_t m_die_uid;
  bool m_is_vector;
};

/// A simple pointer type.
class PointerType : public llvm::RTTIExtends<PointerType, Type> {
public:
  static char ID;

  explicit PointerType(TypeRef pointee_type) : m_pointee_type(pointee_type) {}

  /// The type this pointer points to. E.g., for `int *` this is `int`. Never
  /// null: a `void *` points at the `void` builtin (see TypeRef), so use
  /// BuiltinType::IsVoid to recognize one rather than testing for absence.
  Type *GetPointeeType() const override { return &m_pointee_type.Get(); }

  bool IsPointer() const override { return true; }

  /// True if this is a function-pointer type (`void (*)(int)`), matching
  /// clang's isFunctionPointerType(). A block pointer (`int (^)(int)`) is not
  /// one despite also pointing at a function type, and neither is a *reference*
  /// to a function (`void (&)(int)`) -- which is why this lives on PointerType
  /// rather than being a free function over any pointee. The distinction
  /// matters for formatting: the C++ "function pointer summary" only applies to
  /// pointers, so a function reference must not pick it up.
  bool IsFunctionPointer() const override {
    return !IsBlockPointer() && m_pointee_type.Get().Desugar()->IsFunction();
  }

  // A pointer's size is the target's pointer width, which is a property of the
  // target rather than of the individual pointer, so it isn't stored per
  // pointer: it is recovered from the Context that owns this type (which knows
  // the target triple). Defined out-of-line because it needs Context's
  // definition. See PointerType::GetByteSize in TypeC.cpp.
  std::optional<uint64_t> GetByteSize() const override;

  // A pointer is pointer-aligned, which is its own size -- not the
  // largest-power-of-two-dividing-the-size heuristic Type applies.
  std::optional<uint64_t> GetAlignmentInBits() const override {
    if (std::optional<uint64_t> size = GetByteSize())
      return *size * 8;
    return std::nullopt;
  }

  lldb::Encoding GetEncoding() const override { return lldb::eEncodingUint; }
  lldb::Format GetFormat() const override { return lldb::eFormatHex; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassPointer;
  }
  uint32_t GetTypeInfo() const override;
  Type *GetTransparentChildPointee() override;
  Type *GetNamedMemberPointee() override;

  // A pointer to a record is a C++ construct (so class data formatters still
  // fire), and a pointer to anything else (e.g. `int *`, `enum E *`) a C
  // construct. The latter matters for a null pointer:
  // CPlusPlusLanguage::IsNilReference would print a null C++ pointer as "NULL"
  // instead of its address.
  lldb::LanguageType GetMinimumLanguage() const override;

  Type *
  GetDynamicTypeCandidate(DynamicTypeCheck check,
                          llvm::function_ref<void(Type *)> complete) override;

private:
  TypeRef m_pointee_type;
};

/// A typedef/using alias. It carries its own (alias) name but otherwise behaves
/// like the type it aliases.
class TypedefType
    : public llvm::RTTIExtends<TypedefType, NamedType<SugarType>> {
public:
  using RTTIExtends::RTTIExtends;

  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassTypedef;
  }
  uint32_t GetTypeInfo() const override {
    return SugarType::GetTypeInfo() | lldb::eTypeIsTypedef;
  }
  Type *GetTypedefedType() const override { return GetUnderlyingType(); }
};

/// A `const`- and/or `volatile`-qualified type. Transparent like all sugar; it
/// only records which qualifiers apply so they can be reported and rendered in
/// the type name.
class CVQualifiedType : public llvm::RTTIExtends<CVQualifiedType, SugarType> {
public:
  static char ID;

  CVQualifiedType(TypeRef underlying_type, bool is_const, bool is_volatile)
      : RTTIExtends(underlying_type), m_is_const(is_const),
        m_is_volatile(is_volatile) {}

  bool IsConst() const override { return m_is_const; }
  bool IsVolatile() const { return m_is_volatile; }

  Type *StripCVQualifiers() override {
    return GetUnderlyingType()->StripCVQualifiers();
  }
  // DWARF nests one qualifier per DIE, so `const volatile T` is two stacked
  // CVQualifiedType nodes: OR in whatever the rest of the chain reports.
  unsigned GetCVRQualifiers() const override {
    return (m_is_const ? 0x1 : 0) | (m_is_volatile ? 0x4 : 0) |
           GetUnderlyingType()->GetCVRQualifiers();
  }

private:
  bool m_is_const;
  bool m_is_volatile;
};

/// A C/C++ enumeration type. It has an underlying integer type and a set of
/// named constants; scoped enums (`enum class`) are distinguished so their
/// enumerators are not treated as being in the enclosing scope.
class EnumType
    : public llvm::RTTIExtends<EnumType, NamedType<ByteSizedType<Type>>> {
public:
  static char ID;

  EnumType(Identifier name, DeclScope scope, std::optional<uint64_t> byte_size,
           TypeRef underlying_type, bool is_scoped,
           std::vector<Enumerator> enumerators)
      : RTTIExtends(name, scope, byte_size), m_underlying_type(underlying_type),
        m_is_scoped(is_scoped), m_enumerators(std::move(enumerators)) {}

  /// The integer type backing this enum. Never null: debug info that records
  /// no base type gets the `int` C would have chosen (see
  /// Builder::CreateEnumType).
  Type *GetUnderlyingType() const { return &m_underlying_type.Get(); }
  Type *GetEnumerationIntegerType() const override {
    return GetUnderlyingType();
  }

  bool IsScoped() const { return m_is_scoped; }
  bool IsScopedEnumeration() const override { return IsScoped(); }

  /// True when the underlying integer type is signed.
  bool IsSigned() const {
    return m_underlying_type.Get().GetEncoding() == lldb::eEncodingSint;
  }

  llvm::ArrayRef<Enumerator> GetEnumerators() const override {
    return m_enumerators;
  }

  bool IsInteger() const override {
    lldb::Encoding encoding = GetEncoding();
    return encoding == lldb::eEncodingSint || encoding == lldb::eEncodingUint;
  }
  // Unscoped enumerations promote like the integer types; scoped ones don't
  // convert implicitly at all.
  bool IsPromotableInteger() const override { return !IsScoped(); }

  std::optional<uint64_t> GetAlignmentInBits() const override {
    return m_underlying_type.Get().GetAlignmentInBits();
  }
  lldb::Encoding GetEncoding() const override {
    return m_underlying_type.Get().GetEncoding();
  }
  lldb::Format GetFormat() const override { return lldb::eFormatEnum; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassEnumeration;
  }
  uint32_t GetTypeInfo() const override {
    // Note: TypeSystemClang deliberately does NOT set eTypeIsScalar (or
    // eTypeIsInteger/eTypeIsSigned) here, even though an enum is a scalar in
    // the C++ sense. DIL's cast-verification logic (and other callers) check
    // IsScalarType() before IsEnumerationType(), so setting eTypeIsScalar
    // would misroute enums into the scalar-cast branch instead of the
    // enumeration-cast branch. Match TypeSystemClang's flags exactly.
    return lldb::eTypeIsEnumeration | lldb::eTypeHasValue;
  }

private:
  TypeRef m_underlying_type;
  bool m_is_scoped;
  std::vector<Enumerator> m_enumerators;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_C_H
