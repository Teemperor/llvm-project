//===-- LanguageOpts.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LanguageOpts.h"

#include <utility>

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

LanguageOpts::LanguageOpts(llvm::Triple triple) : m_triple(std::move(triple)) {}
