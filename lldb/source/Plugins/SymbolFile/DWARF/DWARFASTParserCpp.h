//===-- DWARFASTParserCpp.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCPP_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCPP_H

#include "DWARFASTParser.h"
#include "DWARFDIE.h"

#include "llvm/ADT/DenseMap.h"

namespace lldb_private {
class TypeSystemCpp;
namespace cpp_typesystem {
class Type;
class RecordType;
}
} // namespace lldb_private

/// Parses DWARF debug info into the lldb-internal TypeSystemCpp representation.
///
/// This is the TypeSystemCpp counterpart of DWARFASTParserClang. It only knows
/// how to build the small subset of the type system that TypeSystemCpp
/// currently understands (base types and records).
class DWARFASTParserCpp : public lldb_private::plugin::dwarf::DWARFASTParser {
public:
  DWARFASTParserCpp(lldb_private::TypeSystemCpp &ts);

  ~DWARFASTParserCpp() override;

  // LLVM RTTI support
  static bool classof(const DWARFASTParser *Parser) {
    return Parser->GetKind() == Kind::DWARFASTParserCpp;
  }

  // DWARFASTParser interface.
  lldb::TypeSP
  ParseTypeFromDWARF(const lldb_private::SymbolContext &sc,
                     const lldb_private::plugin::dwarf::DWARFDIE &die,
                     bool *type_is_new_ptr) override;

  lldb_private::ConstString ConstructDemangledNameFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {
    return lldb_private::ConstString();
  }

  lldb_private::Function *
  ParseFunctionFromDWARF(lldb_private::CompileUnit &comp_unit,
                         const lldb_private::plugin::dwarf::DWARFDIE &die,
                         lldb_private::AddressRanges func_ranges) override;

  bool CompleteTypeFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die, lldb_private::Type *type,
      const lldb_private::CompilerType &compiler_type) override;

  /// Parse the member functions of \p record, which must have been completed
  /// earlier via CompleteTypeFromDWARF. Member functions are parsed lazily
  /// (only when the expression evaluator needs them to call a method) rather
  /// than as part of record completion, so this is a separate on-demand step
  /// reached through TypeSystemCpp::CompleteMemberFunctions.
  void CompleteMemberFunctionsFromDWARF(
      lldb_private::cpp_typesystem::RecordType &record);

  lldb_private::CompilerDecl GetDeclForUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {
    return lldb_private::CompilerDecl();
  }

  lldb_private::CompilerDeclContext GetDeclContextForUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {
    return lldb_private::CompilerDeclContext();
  }

  lldb_private::CompilerDeclContext GetDeclContextContainingUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {
    return lldb_private::CompilerDeclContext();
  }

  void EnsureAllDIEsInDeclContextHaveBeenParsed(
      lldb_private::CompilerDeclContext decl_context) override {}

  std::string GetDIEClassTemplateParams(
      lldb_private::plugin::dwarf::DWARFDIE die) override;

  // TypeSystemCpp doesn't import types from another AST, so it can't complete
  // types that way.
  bool CanCompleteTypeFromImporter(
      const lldb_private::CompilerType &compiler_type) override {
    return false;
  }

  bool CompleteTypeFromImporter(
      const lldb_private::CompilerType &compiler_type) override {
    return false;
  }

  void MapDeclDIEToDefDIE(
      const lldb_private::plugin::dwarf::DWARFDIE &decl_die,
      const lldb_private::plugin::dwarf::DWARFDIE &def_die) override {}

  // TypeSystemCpp doesn't track clang decl contexts.
  void *GetCachedDeclContext(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {
    return nullptr;
  }

  void LinkCachedDeclContextToDIE(
      void *decl_ctx,
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {}

private:
  lldb::TypeSP
  ParseBaseType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseStructureType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseArrayType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParsePointerType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseReferenceType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseTypedef(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseCVQualifiedType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP ParseEnum(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseFunctionType(const lldb_private::plugin::dwarf::DWARFDIE &die);

  /// Resolve a DWARF type reference to its TypeSystemCpp node. This is the unit
  /// of work spread across threads while completing a record: it may run on a
  /// worker, so it serializes its access to the shared type system through
  /// TypeSystemCpp's lock. Returns nullptr if the reference can't be resolved.
  lldb_private::cpp_typesystem::Type *ResolveReferencedType(
      const lldb_private::plugin::dwarf::DWARFDIE &referencing_die,
      const lldb_private::plugin::dwarf::DWARFDIE &type_die);

  lldb_private::TypeSystemCpp &m_ts;

  /// Maps each record that was completed via CompleteTypeFromDWARF to its
  /// defining DIE, so that CompleteMemberFunctionsFromDWARF can re-find the DIE
  /// to parse the record's member functions on demand (the forward-declaration
  /// map in SymbolFileDWARF is erased once completion starts, so it can't be
  /// reused for this). Populated during completion.
  llvm::DenseMap<lldb_private::cpp_typesystem::Type *,
                 lldb_private::plugin::dwarf::DWARFDIE>
      m_record_defining_die;
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCPP_H
