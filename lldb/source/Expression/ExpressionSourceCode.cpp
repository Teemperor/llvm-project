//===-- ExpressionSourceCode.cpp --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Expression/ExpressionSourceCode.h"

namespace lldb_private {

llvm::Optional<std::string>
ExpressionSourceCode::SaveExpressionTextToTempFile(llvm::StringRef file_prefix,
llvm::StringRef file_suffix,
llvm::StringRef text) {
  int temp_fd;
  llvm::SmallString<128> buffer;
  std::error_code err = llvm::sys::fs::createTemporaryFile(file_prefix,
                                                           file_suffix,
                                                           temp_fd, buffer);
  std::string path = buffer.str().str();
  if (err)
    return llvm::None;
  
  bool success = false;
  lldb_private::File file(temp_fd, true);
  
  size_t bytes_written = text.size();
  
  if (file.Write(text.data(), bytes_written).Success()) {
    if (bytes_written == text.size()) {
      // Make sure we have a newline in the file at the end
      bytes_written = 1;
      file.Write("\n", bytes_written);
      if (bytes_written == 1)
        success = true;
    }
  }
  
  if (!success) {
    llvm::sys::fs::remove(path);
    return llvm::None;
  }
  
  return path;
}

} // namespace lldb_private
