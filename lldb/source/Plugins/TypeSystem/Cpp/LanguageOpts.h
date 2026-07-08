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

namespace lldb_private {
namespace cpp_typesystem {

/// Describes the target/language configuration that determines properties of
/// the type system, such as builtin type sizes and encodings. For now this is
/// just the target triple, but it is the place to grow language-dialect options
/// (C vs C++, standard version, ...) over time.
class LanguageOpts {
public:
  LanguageOpts() = default;
  explicit LanguageOpts(llvm::Triple triple);

  const llvm::Triple &GetTriple() const { return m_triple; }

private:
  llvm::Triple m_triple;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_LANGUAGEOPTS_H
