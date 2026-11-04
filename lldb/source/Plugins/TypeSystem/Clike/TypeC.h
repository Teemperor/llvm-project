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
