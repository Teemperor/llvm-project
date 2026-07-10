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

namespace lldb_private {
class TypeSystemCpp;
namespace cpp_typesystem {
class Type;
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
      lldb_private::plugin::dwarf::DWARFDIE die) override {
    return std::string();
  }

private:
  lldb::TypeSP
  ParseBaseType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseStructureType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseArrayType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParsePointerType(const lldb_private::plugin::dwarf::DWARFDIE &die);

  /// Resolve a DWARF type reference to its TypeSystemCpp node. This is the unit
  /// of work spread across threads while completing a record: it may run on a
  /// worker, so it serializes its access to the shared type system through
  /// TypeSystemCpp's lock. Returns nullptr if the reference can't be resolved.
  lldb_private::cpp_typesystem::Type *ResolveReferencedType(
      const lldb_private::plugin::dwarf::DWARFDIE &referencing_die,
      const lldb_private::plugin::dwarf::DWARFDIE &type_die);

  lldb_private::TypeSystemCpp &m_ts;
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCPP_H
