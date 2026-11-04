//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Context.h"

#include <cassert>

using namespace lldb_private;
using namespace lldb_private::clike;

BuiltinType *Context::GetBuiltinType(llvm::StringRef name,
                                     std::optional<uint64_t> byte_size,
                                     lldb::Encoding encoding,
                                     lldb::Format format) {
  // Prefer the shared canonical instance when the attributes describe one of
  // the enumerated builtin types.
  if (BuiltinType *known = builtin_types.Match(name, encoding, byte_size))
    return known;

  // Otherwise fall back to a bespoke type owned by this Context.
  return Track(std::make_unique<BuiltinType>(
      GetIdentifier(name), byte_size,
      builtin_types.GetAlignmentInBits(encoding, byte_size), encoding, format));
}

TypedefType *Context::CreateTypedefType(llvm::StringRef name,
                                        TypeRef underlying_type,
                                        DeclScope scope) {
  return Track(std::make_unique<TypedefType>(GetIdentifier(name), scope,
                                             underlying_type));
}
