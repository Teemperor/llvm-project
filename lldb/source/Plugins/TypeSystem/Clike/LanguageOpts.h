//===-- LanguageOpts.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_LANGUAGEOPTS_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_LANGUAGEOPTS_H

#include "llvm/TargetParser/Triple.h"

#include "lldb/lldb-enumerations.h"

#include <cstdint>
#include <optional>

namespace llvm {
struct fltSemantics;
} // namespace llvm

namespace lldb_private {
namespace clike_typesystem {

/// Describes the target/language configuration that determines properties of
/// the type system, such as builtin type sizes and encodings. For now this is
/// just the target triple, but it is the place to grow language-dialect options
/// (C vs C++, standard version, ...) over time.
class LanguageOpts {
public:
  /// Sizes (in bytes) of the target-dependent builtin types, derived from the
  /// triple via Clang's target knowledge. Types whose size is fixed by the
  /// language (char/signed char/unsigned char/char8_t == 1) are not listed.
  /// The defaults describe a typical LP64 target and are used when no triple is
  /// available.
  struct BuiltinSizes {
    uint32_t bool_size = 1;
    uint32_t short_size = 2;
    uint32_t int_size = 4;
    uint32_t long_size = 8;
    uint32_t long_long_size = 8;
    uint32_t int128_size = 16;
    uint32_t wchar_size = 4;
    uint32_t char16_size = 2;
    uint32_t char32_size = 4;
    uint32_t float_size = 4;
    uint32_t double_size = 8;
    uint32_t long_double_size = 8;
    uint32_t pointer_size = 8;
  };

  LanguageOpts();
  explicit LanguageOpts(llvm::Triple triple);

  const llvm::Triple &GetTriple() const { return m_triple; }
  const BuiltinSizes &GetBuiltinSizes() const { return m_builtin_sizes; }

  /// The floating point semantics for a float of the given storage size (in
  /// bytes). Mirrors TypeSystemClang::GetFloatTypeSemantics: the size is
  /// matched against the target's float/double/long double/half/__float128
  /// types. Returns APFloat::Bogus() when nothing matches.
  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size,
                                                  lldb::Format format) const;

  /// The storage size (in bytes) of a `_BitInt(bits)` on this target, as clang
  /// would lay it out (the bit width rounded up to the type's ABI alignment).
  /// Returns std::nullopt if the bit width is invalid for the target. Uses only
  /// clang::TargetInfo (ABI facts), staying within the "no clang AST" rule.
  std::optional<uint64_t> GetBitIntByteSize(unsigned bits) const;

private:
  llvm::Triple m_triple;
  BuiltinSizes m_builtin_sizes;

  // Floating point semantics for the target's float types. These point at the
  // static llvm::fltSemantics singletons (stable for the process lifetime), so
  // storing them past the transient TargetInfo used to look them up is safe.
  const llvm::fltSemantics *m_half_semantics;
  const llvm::fltSemantics *m_float_semantics;
  const llvm::fltSemantics *m_double_semantics;
  const llvm::fltSemantics *m_long_double_semantics;
  const llvm::fltSemantics *m_float128_semantics;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_LANGUAGEOPTS_H
