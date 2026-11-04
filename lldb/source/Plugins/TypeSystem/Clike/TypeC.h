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

#include "Identifier.h"
#include "NamedType.h"
#include "SugarType.h"
#include "Type.h"

namespace lldb_private {
namespace clike {

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

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_C_H
