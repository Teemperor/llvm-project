//===-- RecordType.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RecordType.h"

using namespace lldb_private::clike;

// LLVM RTTI.
char RecordType::ID = 0;

std::optional<uint64_t> RecordType::GetAlignmentInBits() const {
  // Explicit alignment such as alignas overrides implicit alignment.
  if (std::optional<uint64_t> align = GetExplicitAlignmentInBits())
    if (*align != 0)
      return *align;
  return Type::GetAlignmentInBits();
}

Type *RecordType::GetHomogeneousAggregateBase(uint32_t &num_fields) const {
  num_fields = 0;
  if (GetNumBaseClasses() > 0 || IsPolymorphic())
    return nullptr;

  bool is_hva = false;
  bool is_hfa = false;
  Type *base_type = nullptr;
  uint64_t base_bitwidth = 0;
  uint32_t count = 0;
  for (uint32_t i = 0, e = GetNumFields(); i != e; ++i) {
    const Field *field = GetFieldAtIndex(i);
    if (!field)
      return nullptr;
    Type *field_type = field->GetType().Get().Desugar();
    uint64_t field_bitwidth = field_type->GetByteSize().value_or(0) * 8;

    if (field_type->GetEncoding() == lldb::eEncodingIEEE754) {
      if (count == 0) {
        base_type = field_type;
      } else {
        if (is_hva)
          return nullptr;
        is_hfa = true;
        if (field_type != base_type)
          return nullptr;
      }
    } else if (field_type->IsVector()) {
      if (count == 0) {
        base_type = field_type;
        base_bitwidth = field_bitwidth;
      } else {
        if (is_hfa)
          return nullptr;
        is_hva = true;
        if (base_bitwidth != field_bitwidth)
          return nullptr;
        if (field_type != base_type)
          return nullptr;
      }
    } else {
      return nullptr;
    }
    ++count;
  }
  if (count == 0)
    return nullptr;
  num_fields = count;
  return base_type;
}
