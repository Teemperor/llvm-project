//===-- LanguageOpts.cpp --------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LanguageOpts.h"

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "clang/Basic/DiagnosticOptions.h"
#include "clang/Basic/TargetInfo.h"
#include "clang/Basic/TargetOptions.h"

#include <memory>
#include <utility>

using namespace lldb_private;
using namespace lldb_private::cpp_typesystem;

LanguageOpts::LanguageOpts(llvm::Triple triple) : m_triple(std::move(triple)) {
  // Ask Clang's target knowledge for the ABI sizes of the builtin types on
  // this triple. This only uses clang::TargetInfo (target/ABI facts), not the
  // Clang AST, so it stays within TypeSystemCpp's "no clang::Decl/Type" rule.
  clang::TargetOptions target_opts;
  target_opts.Triple = m_triple.str();

  clang::DiagnosticOptions diag_opts;
  clang::DiagnosticsEngine diags(
      llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>(), diag_opts);

  // TargetOptions must outlive the returned TargetInfo, which it does here.
  std::unique_ptr<clang::TargetInfo> target(
      clang::TargetInfo::CreateTargetInfo(diags, target_opts));
  if (!target)
    return; // Unknown triple; keep the LP64 defaults.

  auto bytes = [](unsigned bits) -> uint32_t { return bits / 8; };
  m_builtin_sizes.bool_size = bytes(target->getBoolWidth());
  m_builtin_sizes.short_size = bytes(target->getShortWidth());
  m_builtin_sizes.int_size = bytes(target->getIntWidth());
  m_builtin_sizes.long_size = bytes(target->getLongWidth());
  m_builtin_sizes.long_long_size = bytes(target->getLongLongWidth());
  m_builtin_sizes.wchar_size = bytes(target->getWCharWidth());
  m_builtin_sizes.char16_size = bytes(target->getChar16Width());
  m_builtin_sizes.char32_size = bytes(target->getChar32Width());
  m_builtin_sizes.float_size = bytes(target->getFloatWidth());
  m_builtin_sizes.double_size = bytes(target->getDoubleWidth());
  m_builtin_sizes.long_double_size = bytes(target->getLongDoubleWidth());
}
