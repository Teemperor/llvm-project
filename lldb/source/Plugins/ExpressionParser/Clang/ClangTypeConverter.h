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
  /// Peel a deduced `auto`/`decltype(auto)` type down to the type deduction
  /// resolved it to; returns \p qt unchanged if it isn't a deduced type. The
  /// result is null if deduction hasn't run yet (an undeduced `auto`).
  clang::QualType Desugar(clang::QualType qt);

  /// Look up a type the generator synthesized (including cv-qualified variants,
  /// a qualifier-sugared typedef, or the canonical type) in the generator's
  /// reverse map. Sets \p found when \p qt was found there (in which case the
  /// returned type is authoritative, even if invalid); leaves it false to let
  /// Convert fall through to the reconstruction paths below.
  CompilerType ConvertViaReverseMap(clang::QualType qt, bool &found);

  /// Map a clang::BuiltinType the parser created on its own (e.g. the result of
  /// `1 + 1` or a `sizeof`) onto the corresponding TypeSystemCpp builtin.
  /// Returns an invalid CompilerType for a builtin kind we don't model.
  CompilerType ConvertBuiltin(const clang::BuiltinType *bt);

  /// Rebuild a record the parser defined itself (a `struct` written directly in
  /// the expression source, so it has no cpp counterpart) in the target
  /// TypeSystemCpp from the clang record's layout. A complete definition is
  /// rebuilt with its bases/fields; a forward declaration as an incomplete
  /// record.
  CompilerType ConvertRecord(const clang::RecordType *rt);

  /// Rebuild a typedef the parser defined itself (e.g. a persistent typedef,
  /// `typedef int $bar`, which has no cpp counterpart) as a TypeSystemCpp
  /// TypedefType aliasing the recursively-converted underlying type.
  CompilerType ConvertTypedef(const clang::TypedefType *tdt);

  /// Rebuild the `id` / `Class` pseudo-types or an Objective-C class pointer
  /// (`Foo *`) the parser formed, neither of which is in the reverse map.
  CompilerType
  ConvertObjCObjectPointer(const clang::ObjCObjectPointerType *objc_ptr);

  /// Rebuild a simple derived type the parser formed on its own (a reference,
  /// pointer, block pointer, complex, function, vector, or array) from its
  /// recursively-mapped pointee/element.
  CompilerType ConvertDerived(clang::QualType qt);

  ClangASTGenerator &m_generator;
  TypeSystemCpp &m_target;
  clang::ASTContext &m_ast;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGTYPECONVERTER_H
