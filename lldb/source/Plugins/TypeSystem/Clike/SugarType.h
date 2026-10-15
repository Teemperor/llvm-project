//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SUGARTYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SUGARTYPE_H

#include <cstdint>
#include <optional>

#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "Type.h"

namespace lldb_private {
namespace clike {

/// Common base class for sugar types such as typedefs and cv-qualified types.
/// Most queries are forwarded to the underlying type.
class SugarType : public llvm::RTTIExtends<SugarType, Type> {
public:
  static char ID;

  explicit SugarType(TypeRef underlying_type)
      : m_underlying_type(underlying_type) {}

  /// The type wrapped by this sugar. Never null.
  Type *GetUnderlyingType() const { return &m_underlying_type.Get(); }

  Type *Desugar() override { return m_underlying_type.Get().Desugar(); }
  bool IsAggregate() const override {
    return m_underlying_type.Get().IsAggregate();
  }
  bool IsComplete() const override {
    return m_underlying_type.Get().IsComplete();
  }
  // Not cached, as the size of the underlying type might change on completion.
  std::optional<uint64_t> GetByteSize() const override {
    return m_underlying_type.Get().GetByteSize();
  }
  std::optional<uint64_t> GetAlignmentInBits() const override {
    return m_underlying_type.Get().GetAlignmentInBits();
  }
  lldb::Encoding GetEncoding() const override {
    return m_underlying_type.Get().GetEncoding();
  }
  lldb::Format GetFormat() const override {
    return m_underlying_type.Get().GetFormat();
  }
  lldb::TypeClass GetTypeClass() const override {
    return m_underlying_type.Get().GetTypeClass();
  }
  uint32_t GetTypeInfo() const override {
    return m_underlying_type.Get().GetTypeInfo();
  }
  uint32_t GetNumFields() const override {
    return m_underlying_type.Get().GetNumFields();
  }
  const Field *GetFieldAtIndex(uint32_t idx) const override {
    return m_underlying_type.Get().GetFieldAtIndex(idx);
  }
  uint32_t GetNumBaseClasses() const override {
    return m_underlying_type.Get().GetNumBaseClasses();
  }
  const BaseClass *GetBaseClassAtIndex(uint32_t idx) const override {
    return m_underlying_type.Get().GetBaseClassAtIndex(idx);
  }
  std::optional<PtrAuthQualifier> GetPtrAuthQualifier() const override {
    return m_underlying_type.Get().GetPtrAuthQualifier();
  }

private:
  TypeRef m_underlying_type;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_SUGARTYPE_H
