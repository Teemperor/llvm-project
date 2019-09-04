//===-- IOObjectTest.cpp ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Utility/IOObject.h"
#include "lldb/Utility/Status.h"
#include "gtest/gtest.h"

#include <list>

using namespace lldb_private;
using namespace lldb;

namespace {
/// Simulates an output that has a buffer. The buffer is specified by a list
/// of buffer sizes that are used one after the other in the given order.
///
/// If the end of the buffer list is reached, any Write calls will fail with
/// a generic error. This can be used to simulate a IOObject that will fail
/// to Write bytes at some point.
class BufferedOutputFile : public IOObject {
  /// The list of buffers sizes that should be used (stored in the order
  /// they should be used in).
  /// We remove the first buffer from the list when we start using it.
  std::list<size_t> m_buffer_sizes;
  /// The space left in the current buffer. Note that there is no actual buffer.
  /// This class only keep strack of the buffer size to simulate one.
  size_t m_buffer_left = 0;
  /// Get the next buffer from the buffer list and remove it from the list.
  size_t PopNextBufferSize() {
    assert(!m_buffer_sizes.empty());
    size_t result = m_buffer_sizes.front();
    m_buffer_sizes.pop_front();
    return result;
  }
  /// The bytes that were written in the order they were written.
  std::string m_output;

public:
  BufferedOutputFile(std::list<size_t> buffer_sizes)
      : IOObject(eFDTypeFile, false), m_buffer_sizes(buffer_sizes) {
    if (!m_buffer_sizes.empty())
      m_buffer_left = PopNextBufferSize();
  }

  Status Write(const void *buf, size_t &num_bytes) override {
    // If the end of the current buffer is reached, grab a new buffer from the
    // list or throw an error if the end of the list is reached.
    if (m_buffer_left == 0) {
      if (m_buffer_sizes.empty()) {
        Status error;
        error.SetErrorToGenericError();
        return error;
      }
      num_bytes = 0;
      m_buffer_left = PopNextBufferSize();
      return Status();
    }
    // Pretend to fill the buffer.
    num_bytes = std::min(m_buffer_left, num_bytes);
    m_buffer_left -= num_bytes;

    // Store the bytes that were written so that they can be checked later.
    llvm::StringRef as_str(static_cast<const char *>(buf), num_bytes);
    m_output.append(as_str.str());
    return Status();
  }

  const char *GetOutput() { return m_output.c_str(); }

  // Other methods that we don't use.
  Status Read(void *buf, size_t &num_bytes) override {
    assert(false && "read not supported");
  }
  bool IsValid() const override { return true; }
  Status Close() override { return Status(); }
  WaitableHandle GetWaitableHandle() override { return WaitableHandle(); }
};
} // namespace

static void TestBufferWriteAll(const char *data,
                               std::list<size_t> buffer_sizes) {
  BufferedOutputFile out(buffer_sizes);
  Status error = out.WriteAll(data);
  EXPECT_TRUE(error.Success());
  EXPECT_STREQ(data, out.GetOutput());
}

TEST(IOObjectWriteAll, BufferTooSmall) {
  TestBufferWriteAll("foobar", {1, 1, 1, 1, 1, 1, 1});
  TestBufferWriteAll("foobar", {1, 2, 1, 2, 1, 2, 1});
}

TEST(IOObjectWriteAll, GrowingBuffer) {
  TestBufferWriteAll("foobar", {1, 2, 3, 4, 5});
}

TEST(IOObjectWriteAll, ShrinkingBuffer) {
  TestBufferWriteAll("foobar", {3, 2, 1, 1, 1});
}

TEST(IOObjectWriteAll, BufferTooLarge) { TestBufferWriteAll("foobar", {1000}); }

TEST(IOObjectWriteAll, BufferSameSize) {
  TestBufferWriteAll("foobar", {strlen("foobar"), 1});
}

TEST(IOObjectWriteAll, ZeroBuffers) {
  // Usually write/send don't return 0, but LLDB should still handle this case
  // if for some reason there is a backend that can write 0 bytes (or LLDB runs
  // on an OS that can have 0-return values from write/send).
  TestBufferWriteAll("foobar", {1, 1, 1, 0, 1, 1, 1});
  TestBufferWriteAll("foobar", {1, 1, 1, 0, 0, 1, 1, 1});
  TestBufferWriteAll("foobar", {1, 1, 0, 1, 1, 0, 1, 1});
  TestBufferWriteAll("foobar", {0, 1, 1, 0, 1, 1, 0, 1, 1});
  TestBufferWriteAll("foobar", {0, 0, 1, 1, 0, 1, 1, 0, 1, 1});
}

TEST(IOObjectWriteAll, FailToWrite) {
  /* No buffers = instant error when trying to write*/
  BufferedOutputFile out({});
  Status error = out.WriteAll("foobar");
  EXPECT_TRUE(error.Fail());
  EXPECT_STREQ("", out.GetOutput());
}

TEST(IOObjectWriteAll, InterruptedWrite) {
  // Two small buffers that are not enough to store the string.
  BufferedOutputFile out({1, 1});
  Status error = out.WriteAll("foobar");
  EXPECT_TRUE(error.Fail());
  EXPECT_STREQ("fo", out.GetOutput());
}
