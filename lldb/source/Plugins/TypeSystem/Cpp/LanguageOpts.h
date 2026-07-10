//===-- LanguageOpts.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_LANGUAGEOPTS_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_LANGUAGEOPTS_H

#include "llvm/TargetParser/Triple.h"

#include <cstdint>

namespace lldb_private {
namespace cpp_typesystem {

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
  };

  LanguageOpts() = default;
  explicit LanguageOpts(llvm::Triple triple);

  const llvm::Triple &GetTriple() const { return m_triple; }
  const BuiltinSizes &GetBuiltinSizes() const { return m_builtin_sizes; }

private:
  llvm::Triple m_triple;
  BuiltinSizes m_builtin_sizes;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_LANGUAGEOPTS_H
