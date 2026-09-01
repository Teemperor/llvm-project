//===-- TypeSystemClikeThreadSafetyTest.cpp ------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/TypeSystem/Clike/Builder.h"
#include "Plugins/TypeSystem/Clike/TypeSystemClike.h"

#include "gtest/gtest.h"

#include <atomic>
#include <thread>
#include <vector>

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

namespace {
struct TypeSystemClikeThreadSafetyTest : public testing::Test {
  std::shared_ptr<TypeSystemClike> ts = std::make_shared<TypeSystemClike>(
      "test", llvm::Triple("x86_64-pc-linux-gnu"));
  Builder builder{*ts};

  CompilerType GetInt() {
    return builder.GetBuiltinType("int", 4, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  }

  /// Create a complete record with one `int` field named `x`.
  CompilerType MakeRecordWithField(llvm::StringRef record_name,
                                   llvm::StringRef field_name) {
    CompilerType record = builder.CreateRecordType(record_name, 4, false);
    auto *r = llvm::cast<RecordType>(
        static_cast<clike_typesystem::Type *>(record.GetOpaqueQualType()));
    builder.AddField(
        *r, builder.GetIdentifier(field_name),
        static_cast<clike_typesystem::Type *>(GetInt().GetOpaqueQualType()),
        0);
    builder.SetRecordComplete(*r);
    return record;
  }
};
} // namespace

// Exercises the read/write lock added to TypeSystemClike's own query API: a
// pool of threads repeatedly take the write lock (GetPointerType, which
// allocates a fresh PointerType node into the shared Context) while another
// pool concurrently takes the read lock (IsPointerOrReferenceType/
// GetTypeClass/GetNumFields/GetFieldAtIndex) against the record being pointed
// to. This doesn't prove the absence of races on its own (that needs a TSan
// build), but it does drive every lock acquisition path added by this change
// under real contention, so a plain logic bug -- e.g. a method re-entering
// its own lock on the same thread -- reliably hangs the test instead of
// passing by accident on a lightly loaded machine.
TEST_F(TypeSystemClikeThreadSafetyTest, ConcurrentReadersAndWriters) {
  CompilerType record = MakeRecordWithField("Foo", "x");
  lldb::opaque_compiler_type_t record_type = record.GetOpaqueQualType();

  constexpr int kNumWriterThreads = 4;
  constexpr int kNumReaderThreads = 4;
  constexpr int kIterations = 500;

  std::atomic<bool> failed{false};
  std::vector<std::thread> threads;

  for (int i = 0; i < kNumWriterThreads; ++i) {
    threads.emplace_back([&] {
      for (int j = 0; j < kIterations; ++j) {
        CompilerType ptr = ts->GetPointerType(record_type);
        if (!ptr || !ts->IsPointerType(ptr.GetOpaqueQualType(), nullptr))
          failed = true;
      }
    });
  }
  for (int i = 0; i < kNumReaderThreads; ++i) {
    threads.emplace_back([&] {
      for (int j = 0; j < kIterations; ++j) {
        if (ts->IsPointerOrReferenceType(record_type, nullptr))
          failed = true;
        if (ts->GetTypeClass(record_type) == lldb::eTypeClassInvalid)
          failed = true;
        if (ts->GetNumFields(record_type) != 1)
          failed = true;
        std::string name;
        uint64_t bit_offset = 0;
        uint32_t bitfield_size = 0;
        bool is_bitfield = false;
        ts->GetFieldAtIndex(record_type, 0, name, &bit_offset,
                            &bitfield_size, &is_bitfield);
        if (name != "x")
          failed = true;
      }
    });
  }

  for (std::thread &t : threads)
    t.join();

  EXPECT_FALSE(failed);
}
