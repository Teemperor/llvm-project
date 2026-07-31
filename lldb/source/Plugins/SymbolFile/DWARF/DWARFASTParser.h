//===-- DWARFASTParser.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H

#include "DWARFDefines.h"
#include "lldb/Core/PluginInterface.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/CompilerDecl.h"
#include "lldb/Symbol/CompilerDeclContext.h"
#include "lldb/lldb-enumerations.h"
#include <optional>

namespace lldb_private {
class CompileUnit;
class ExecutionContext;
}

namespace lldb_private::plugin {
namespace dwarf {
class DWARFDIE;
class SymbolFileDWARF;

class DWARFASTParser {
public:
  enum class Kind { DWARFASTParserClang, DWARFASTParserClike };
  DWARFASTParser(Kind kind) : m_kind(kind) {}

  virtual ~DWARFASTParser() = default;

  virtual lldb::TypeSP ParseTypeFromDWARF(const SymbolContext &sc,
                                          const DWARFDIE &die,
                                          bool *type_is_new_ptr) = 0;

  virtual ConstString ConstructDemangledNameFromDWARF(const DWARFDIE &die) = 0;

  virtual Function *ParseFunctionFromDWARF(CompileUnit &comp_unit,
                                           const DWARFDIE &die,
                                           AddressRanges ranges) = 0;

  virtual bool CompleteTypeFromDWARF(const DWARFDIE &die, Type *type,
                                     const CompilerType &compiler_type) = 0;

  virtual CompilerDecl GetDeclForUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual CompilerDeclContext
  GetDeclContextForUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual CompilerDeclContext
  GetDeclContextContainingUIDFromDWARF(const DWARFDIE &die) = 0;

  virtual void EnsureAllDIEsInDeclContextHaveBeenParsed(
      CompilerDeclContext decl_context) = 0;

  virtual std::string GetDIEClassTemplateParams(DWARFDIE die) = 0;

  /// Returns true if this parser can complete \p compiler_type via an AST
  /// importer (i.e. the type was minimally imported from another AST). Parsers
  /// that don't import types return false.
  virtual bool CanCompleteTypeFromImporter(const CompilerType &compiler_type) = 0;

  /// Completes \p compiler_type via the parser's AST importer. Returns true if
  /// the type was completed. Only valid to call when
  /// CanCompleteTypeFromImporter() returned true.
  virtual bool CompleteTypeFromImporter(const CompilerType &compiler_type) = 0;

  /// Records that \p def_die is the definition DIE for the forward-declared
  /// \p decl_die, so later lookups resolve to the definition.
  virtual void MapDeclDIEToDefDIE(const DWARFDIE &decl_die,
                                  const DWARFDIE &def_die) = 0;

  /// Returns the parser-specific declaration context cached for \p die, or
  /// nullptr. The returned pointer is opaque to callers and only meaningful to
  /// the parser that produced it; it is used to relink uniqued class method
  /// DIEs. Parsers that don't track decl contexts return nullptr.
  virtual void *GetCachedDeclContext(const DWARFDIE &die) = 0;

  /// Associates \p die with the opaque decl context \p decl_ctx previously
  /// obtained from GetCachedDeclContext.
  virtual void LinkCachedDeclContextToDIE(void *decl_ctx,
                                          const DWARFDIE &die) = 0;

  static std::optional<SymbolFile::ArrayInfo>
  ParseChildArrayInfo(const DWARFDIE &parent_die,
                      const ExecutionContext *exe_ctx = nullptr);

  lldb_private::Type *GetTypeForDIE(const DWARFDIE &die);

  static lldb::AccessType GetAccessTypeFromDWARF(uint32_t dwarf_accessibility);

  Kind GetKind() const { return m_kind; }

private:
  const Kind m_kind;
};
} // namespace dwarf
} // namespace lldb_private::plugin

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSER_H
