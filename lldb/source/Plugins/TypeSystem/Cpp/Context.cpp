//===-- Context.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Context.h"

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

BuiltinType *Context::CreateBuiltinType(llvm::StringRef name,
                                        std::optional<uint64_t> byte_size,
                                        lldb::Encoding encoding) {
  auto type = std::make_unique<BuiltinType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  type->SetEncoding(encoding);
  return Track(std::move(type));
}

StructType *Context::CreateRecordType(llvm::StringRef name,
                                      std::optional<uint64_t> byte_size) {
  auto type = std::make_unique<StructType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  return Track(std::move(type));
}
