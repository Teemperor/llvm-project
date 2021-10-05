//===-- llvm/Support/raw_ostream_multiplexer.h ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains a raw_ostream multiplexer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_RAW_OSTREAM_MULTIPLEXER_H
#define LLVM_SUPPORT_RAW_OSTREAM_MULTIPLEXER_H

#include "llvm/Support/raw_ostream.h"
#include <vector>
#include <memory>

namespace llvm {
  class raw_ostream_multiplexer : public raw_ostream {
    std::vector<std::unique_ptr<raw_ostream>> Streams;

    void write_impl(const char *Ptr, size_t Size) override {
      for (std::unique_ptr<raw_ostream> &S : Streams)
        S->write(Ptr, Size);
    }

    uint64_t current_pos() const override { return 0; }

  public:
    raw_ostream_multiplexer() {
      SetUnbuffered();
    }
    ~raw_ostream_multiplexer() override;

    void reserveExtraSpace(uint64_t ExtraSize) override {
      for (std::unique_ptr<raw_ostream> &S : Streams)
        S->reserveExtraSpace(ExtraSize);
    }
  };
} // end llvm namespace

#endif
