//===-- IOObject.cpp --------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/IOObject.h"
#include "lldb/Utility/Status.h"

using namespace lldb_private;

const IOObject::WaitableHandle IOObject::kInvalidHandleValue = -1;
IOObject::~IOObject() = default;

Status IOObject::WriteAll(const void *buf, size_t num_bytes) {
  const char *byte_buf = static_cast<const char *>(buf);
  const size_t total_bytes = num_bytes;
  size_t total_bytes_written = 0;
  while (total_bytes_written < total_bytes) {
    size_t bytes_to_write = total_bytes - total_bytes_written;
    Status error = Write(byte_buf, bytes_to_write);
    if (error.Fail())
      return error;
    // Write() changed bytes_to_write to the number of bytes written...
    total_bytes_written += bytes_to_write;
    byte_buf += bytes_to_write;
  }
  return Status();
}
Status IOObject::WriteAll(llvm::StringRef data) {
  return WriteAll(data.data(), data.size());
}
