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

BuiltinType *Context::GetBuiltinType(llvm::StringRef name,
                                     std::optional<uint64_t> byte_size,
                                     lldb::Encoding encoding,
                                     lldb::Format format) {
  // Prefer the shared canonical instance when the attributes describe one of
  // the enumerated builtin types.
  if (BuiltinType *known = builtin_types.Match(name, encoding, byte_size))
    return known;

  // Otherwise fall back to a bespoke type owned by this Context.
  auto type = std::make_unique<BuiltinType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  type->SetEncoding(encoding);
  type->SetFormat(format);
  return Track(std::move(type));
}

RecordType *Context::CreateRecordType(llvm::StringRef name,
                                      std::optional<uint64_t> byte_size,
                                      bool is_cpp_class) {
  std::unique_ptr<RecordType> type;
  if (is_cpp_class)
    type = std::make_unique<ClassType>();
  else
    type = std::make_unique<StructType>();
  type->SetName(GetIdentifier(name));
  type->SetByteSize(byte_size);
  return Track(std::move(type));
}

ArrayType *Context::CreateArrayType(Type *element_type,
                                    std::optional<uint64_t> num_elements) {
  auto type = std::make_unique<ArrayType>();
  type->SetElementType(element_type);
  type->SetNumElements(num_elements);
  // The array's storage is the element size times the element count (when both
  // are known).
  if (num_elements)
    if (std::optional<uint64_t> elem_size = element_type->GetByteSize())
      type->SetByteSize(*elem_size * *num_elements);
  return Track(std::move(type));
}

PointerType *Context::CreatePointerType(Type *pointee_type) {
  auto type = std::make_unique<PointerType>();
  type->SetPointeeType(pointee_type);
  type->SetByteSize(m_opts.GetBuiltinSizes().pointer_size);
  return Track(std::move(type));
}
