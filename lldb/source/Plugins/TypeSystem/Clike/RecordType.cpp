//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "RecordType.h"

#include <algorithm>

using namespace lldb_private::clike;

// LLVM RTTI.
char RecordType::ID = 0;

std::optional<uint64_t>
RecordType::ComputeAlignmentInBits(uint64_t min_alignment_in_bits) const {
  // Explicit alignment such as alignas overrides implicit alignment.
  if (std::optional<uint64_t> align = GetExplicitAlignmentInBits())
    if (*align != 0)
      return *align;
  if (!IsComplete())
    return std::nullopt;

  // The debug info doesn't say whether a record is packed. Like Clang does for
  // records with an external layout, treat the record as packed (i.e., byte
  // aligned) if a member isn't at a multiple of its alignment or if the size
  // isn't a multiple of the alignment.
  uint64_t alignment = min_alignment_in_bits;
  bool is_packed = false;
  auto add_member = [&](const Type &type, std::optional<uint64_t> byte_offset) {
    // Members with an unknown alignment (e.g., a base class without a
    // definition) don't contribute.
    std::optional<uint64_t> member_alignment = type.GetAlignmentInBits();
    if (!member_alignment || *member_alignment == 0)
      return;
    alignment = std::max(alignment, *member_alignment);
    if (byte_offset && (*byte_offset * 8) % *member_alignment != 0)
      is_packed = true;
  };

  for (uint32_t i = 0, e = GetNumBaseClasses(); i != e; ++i) {
    const BaseClass *base = GetBaseClassAtIndex(i);
    // The offset of a virtual base isn't known statically.
    add_member(base->GetType().Get(),
               base->IsVirtual()
                   ? std::nullopt
                   : std::optional<uint64_t>(base->GetByteOffset()));
  }
  for (uint32_t i = 0, e = GetNumFields(); i != e; ++i) {
    const Field *field = GetFieldAtIndex(i);
    if (!field->IsBitfield()) {
      add_member(field->GetType().Get(), field->GetByteOffset());
      continue;
    }
    // Unnamed bitfields don't affect the alignment. The storage unit offset of
    // a bitfield says nothing about packing.
    if (!field->GetName().GetName().empty())
      add_member(field->GetType().Get(), std::nullopt);
  }

  if (std::optional<uint64_t> byte_size = GetByteSize())
    if ((*byte_size * 8) % alignment != 0)
      is_packed = true;
  return is_packed ? 8 : alignment;
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
