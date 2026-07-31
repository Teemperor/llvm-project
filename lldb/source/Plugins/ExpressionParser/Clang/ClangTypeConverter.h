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
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"

namespace clang {
class ASTContext;
class ObjCInterfaceDecl;
class ObjCMethodDecl;
} // namespace clang

namespace lldb_private {

class ClangASTGenerator;
class TypeSystemClike;

namespace clike_typesystem {
class ObjCInterfaceType;
} // namespace clike_typesystem

/// Maps a Clang type produced by a ClangASTGenerator back onto its
/// clike_typesystem origin, so an expression's result type can be expressed as a
/// TypeSystemClike type without ever translating Clang types back "for real".
///
/// This is the inverse direction of ClangASTGenerator (which maps
/// clike_typesystem -> Clang). It relies on the generator's reverse map (Clang
/// type -> originating clike_typesystem type) for everything the generator
/// synthesized, and reconstructs the remainder (builtins, cv-qualifiers,
/// parser-defined records, and simple derived types the parser formed on its
/// own) directly from the Clang type.
///
/// All reconstructed types are created in the target TypeSystemClike (typically
/// the scratch TypeSystemClike that owns expression result/persistent types),
/// which is fixed for the lifetime of the converter.
class ClangTypeConverter {
public:
  /// \param generator the generator whose reverse map (Clang type ->
  /// clike_typesystem type) is consulted; must outlive this converter.
  /// \param target the TypeSystemClike that owns every reconstructed type.
  /// \param source_ast the clang::ASTContext the converted types live in. It
  /// defaults to the generator's own AST (the expression AST), used by the
  /// result-type / persistent-decl paths. Pass a different context (e.g. the
  /// ClangModulesDeclVendor's ASTContext) to reconstruct types that were
  /// produced elsewhere: those are never in the generator's reverse map, so the
  /// converter falls through to its reconstruction paths, which then query
  /// layout/sugar from this (correct) context.
  ClangTypeConverter(ClangASTGenerator &generator, TypeSystemClike &target,
                     clang::ASTContext *source_ast = nullptr);

  /// Map \p qt (a Clang type) back to its clike_typesystem origin. Returns an
  /// invalid CompilerType if the type can't be mapped. The returned
  /// CompilerType is owned by the target TypeSystemClike.
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
  /// `1 + 1` or a `sizeof`) onto the corresponding TypeSystemClike builtin.
  /// Returns an invalid CompilerType for a builtin kind we don't model.
  CompilerType ConvertBuiltin(const clang::BuiltinType *bt);

  /// Rebuild a record the parser defined itself (a `struct` written directly in
  /// the expression source, so it has no cpp counterpart) in the target
  /// TypeSystemClike from the clang record's layout. A complete definition is
  /// rebuilt with its bases/fields; a forward declaration as an incomplete
  /// record.
  CompilerType ConvertRecord(const clang::RecordType *rt);

  /// Rebuild a typedef the parser defined itself (e.g. a persistent typedef,
  /// `typedef int $bar`, which has no cpp counterpart) as a TypeSystemClike
  /// TypedefType aliasing the recursively-converted underlying type.
  CompilerType ConvertTypedef(const clang::TypedefType *tdt);

  /// Record \p decl's enclosing namespace chain (skipping non-namespace decl
  /// contexts, e.g. a linkage-spec) and unqualified name on \p type, mirroring
  /// DWARFASTParserClike::SetTypeNameInfo. Without this a type reconstructed
  /// from real module code (e.g. `std::size_t`, resolved by the parser rather
  /// than found in the reverse map) has no DeclContext, so its display name
  /// prints unqualified (`size_t` instead of `std::size_t`).
  void SetTypeNameInfo(const clang::NamedDecl *decl, CompilerType type);

  /// Rebuild the `id` / `Class` pseudo-types or an Objective-C class pointer
  /// (`Foo *`) the parser formed, neither of which is in the reverse map.
  CompilerType
  ConvertObjCObjectPointer(const clang::ObjCObjectPointerType *objc_ptr);

  /// Rebuild a simple derived type the parser formed on its own (a reference,
  /// pointer, block pointer, complex, function, vector, or array) from its
  /// recursively-mapped pointee/element.
  CompilerType ConvertDerived(clang::QualType qt);

public:
  /// Transport an Objective-C interface (`@interface`) that lives in \p m_ast
  /// (e.g. the ClangModulesDeclVendor's ASTContext) into an equivalent
  /// TypeSystemClike ObjCInterfaceType owned by the target. When \p complete is
  /// set the interface is fully populated (superclass, ivars, and every
  /// instance/class method + property accessor, with their real typedef'd
  /// signatures -- unlike the ObjC runtime, which loses typedef names such as
  /// `NSUInteger`); otherwise a name-only forward declaration is produced (used
  /// for the classes a method signature merely refers to through a pointer).
  /// Interfaces are cached by decl so a reference cycle terminates and the same
  /// interface maps to a single cpp type.
  CompilerType ConvertObjCInterface(const clang::ObjCInterfaceDecl *decl,
                                    bool complete);

private:
  /// Populate the (already-created, registered) cpp interface \p iface_ct from
  /// the complete module interface \p def: superclass, ivars, methods, and
  /// property accessor methods.
  void FillObjCInterface(const clang::ObjCInterfaceDecl *def,
                         CompilerType iface_ct);

  /// Add a single Objective-C method (from a module ObjCMethodDecl) to the cpp
  /// interface being built, translating its return/parameter types.
  void AddObjCMethod(clike_typesystem::ObjCInterfaceType &iface,
                     const clang::ObjCInterfaceDecl *def,
                     const clang::ObjCMethodDecl *method);

  ClangASTGenerator &m_generator;
  TypeSystemClike &m_target;
  clang::ASTContext &m_ast;

  /// Interfaces already created (forward or complete), keyed by definition (or
  /// canonical) decl, so a reference cycle terminates and identity is stable.
  llvm::DenseMap<const clang::ObjCInterfaceDecl *, CompilerType> m_objc_ifaces;
  /// Interfaces already fully populated (a subset of m_objc_ifaces' keys).
  llvm::DenseSet<const clang::ObjCInterfaceDecl *> m_objc_completed;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGTYPECONVERTER_H
