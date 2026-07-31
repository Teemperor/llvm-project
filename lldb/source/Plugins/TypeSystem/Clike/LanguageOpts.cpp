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

#include "llvm/ADT/APFloat.h"
#include "llvm/Support/MathExtras.h"

#include <memory>
#include <utility>

using namespace lldb_private;
using namespace lldb_private::clike_typesystem;

LanguageOpts::LanguageOpts()
    : m_half_semantics(&llvm::APFloat::IEEEhalf()),
      m_float_semantics(&llvm::APFloat::IEEEsingle()),
      m_double_semantics(&llvm::APFloat::IEEEdouble()),
      // The LP64 defaults describe an 8-byte (IEEE double) long double.
      m_long_double_semantics(&llvm::APFloat::IEEEdouble()),
      m_float128_semantics(&llvm::APFloat::IEEEquad()) {}

LanguageOpts::LanguageOpts(llvm::Triple triple) : LanguageOpts() {
  m_triple = std::move(triple);

  // Ask Clang's target knowledge for the ABI sizes of the builtin types on
  // this triple. This only uses clang::TargetInfo (target/ABI facts), not the
  // Clang AST, so it stays within TypeSystemClike's "no clang::Decl/Type" rule.
  clang::TargetOptions target_opts;
  target_opts.Triple = m_triple.str();

  clang::DiagnosticOptions diag_opts;
  clang::DiagnosticsEngine diags(llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>(),
                                 diag_opts, new clang::IgnoringDiagConsumer());

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
  m_builtin_sizes.pointer_size = bytes(target->getPointerWidth(clang::LangAS::Default));

  // The fltSemantics returned here are references to static singletons, so
  // caching their addresses past the transient TargetInfo is safe.
  m_half_semantics = &target->getHalfFormat();
  m_float_semantics = &target->getFloatFormat();
  m_double_semantics = &target->getDoubleFormat();
  m_long_double_semantics = &target->getLongDoubleFormat();
  m_float128_semantics = &target->getFloat128Format();
}

const llvm::fltSemantics &
LanguageOpts::GetFloatTypeSemantics(size_t byte_size,
                                    lldb::Format format) const {
  // Mirror TypeSystemClang::GetFloatTypeSemantics: match the storage size
  // against the target's float types, in the same order.
  const size_t bit_size = byte_size * 8;
  if (bit_size == m_builtin_sizes.float_size * 8u)
    return *m_float_semantics;
  if (bit_size == m_builtin_sizes.double_size * 8u)
    return *m_double_semantics;
  if (format == lldb::eFormatFloat128 && bit_size == 128)
    return *m_float128_semantics;
  if (bit_size == m_builtin_sizes.long_double_size * 8u ||
      bit_size == llvm::APFloat::semanticsSizeInBits(*m_long_double_semantics))
    return *m_long_double_semantics;
  if (bit_size == 16)
    return *m_half_semantics;
  if (bit_size == 128)
    return *m_float128_semantics;
  return llvm::APFloat::Bogus();
}

std::optional<uint64_t> LanguageOpts::GetBitIntByteSize(unsigned bits) const {
  if (bits == 0)
    return std::nullopt;

  // Recreate a transient TargetInfo (this is a rare lookup path) to ask for the
  // ABI layout of a `_BitInt` of this width. This uses only clang::TargetInfo,
  // not the Clang AST, so it stays within TypeSystemClike's rule.
  clang::TargetOptions target_opts;
  target_opts.Triple = m_triple.str();

  clang::DiagnosticOptions diag_opts;
  clang::DiagnosticsEngine diags(llvm::makeIntrusiveRefCnt<clang::DiagnosticIDs>(),
                                 diag_opts, new clang::IgnoringDiagConsumer());

  std::unique_ptr<clang::TargetInfo> target(
      clang::TargetInfo::CreateTargetInfo(diags, target_opts));
  if (!target)
    return std::nullopt;

  if (bits > target->getMaxBitIntWidth())
    return std::nullopt;

  // clang lays out a `_BitInt` by rounding its width up to its ABI alignment.
  unsigned align_bits = target->getBitIntAlign(bits);
  uint64_t size_bits = llvm::alignTo(bits, align_bits);
  return size_bits / 8;
}
