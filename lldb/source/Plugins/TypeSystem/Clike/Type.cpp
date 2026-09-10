//===-- Type.cpp ----------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Type.h"

using namespace lldb_private::clike;

// LLVM RTTI.
char Type::ID = 0;

std::optional<uint64_t> Type::GetAlignmentInBits() const {
  std::optional<uint64_t> byte_size = GetByteSize();
  if (!byte_size || *byte_size == 0)
    return std::nullopt;
  uint64_t align_bytes = 1;
  while (align_bytes * 2 <= 8 && (*byte_size % (align_bytes * 2)) == 0)
    align_bytes *= 2;
  return align_bytes * 8;
}

bool Type::HasFields(llvm::function_ref<void(Type *)> complete) {
  Type *t = Desugar();
  complete(t);
  if (t->GetNumFields() != 0)
    return true;
  // A record without a definition (e.g., due to -flimit-debug-info) is
  // treated as having fields so that it isn't hidden as an empty base class.
  if (!t->IsComplete())
    return true;
  for (uint32_t i = 0, n = t->GetNumBaseClasses(); i < n; ++i) {
    const BaseClass *base = t->GetBaseClassAtIndex(i);
    if (base && base->GetType().Get().HasFields(complete))
      return true;
  }
  return false;
}
