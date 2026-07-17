//===-- ClangTypeConverter.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGTYPECONVERTER_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGTYPECONVERTER_H

#include "lldb/Symbol/CompilerType.h"

#include "clang/AST/Type.h"

namespace clang {
class ASTContext;
} // namespace clang

namespace lldb_private {

class ClangASTGenerator;
class TypeSystemCpp;

/// Maps a Clang type produced by a ClangASTGenerator back onto its
/// cpp_typesystem origin, so an expression's result type can be expressed as a
/// TypeSystemCpp type without ever translating Clang types back "for real".
///
/// This is the inverse direction of ClangASTGenerator (which maps
/// cpp_typesystem -> Clang). It relies on the generator's reverse map (Clang
/// type -> originating cpp_typesystem type) for everything the generator
/// synthesized, and reconstructs the remainder (builtins, cv-qualifiers,
/// parser-defined records, and simple derived types the parser formed on its
/// own) directly from the Clang type.
///
/// All reconstructed types are created in the target TypeSystemCpp (typically
/// the scratch TypeSystemCpp that owns expression result/persistent types),
/// which is fixed for the lifetime of the converter.
class ClangTypeConverter {
public:
  /// \param generator the generator whose reverse map (Clang type ->
  /// cpp_typesystem type) is consulted; must outlive this converter.
  /// \param target the TypeSystemCpp that owns every reconstructed type.
  ClangTypeConverter(ClangASTGenerator &generator, TypeSystemCpp &target);

  /// Map \p qt (a Clang type) back to its cpp_typesystem origin. Returns an
  /// invalid CompilerType if the type can't be mapped. The returned
  /// CompilerType is owned by the target TypeSystemCpp.
  CompilerType Convert(clang::QualType qt);

private:
  ClangASTGenerator &m_generator;
  TypeSystemCpp &m_target;
  clang::ASTContext &m_ast;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGTYPECONVERTER_H
