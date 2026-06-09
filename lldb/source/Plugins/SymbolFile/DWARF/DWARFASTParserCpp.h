//===-- DWARFASTParserCpp.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCPP_H
#define LLDB_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCPP_H

#include "DWARFASTParser.h"
#include "DWARFDIE.h"
#include "Plugins/TypeSystem/Clang/LLDBTypeIR.h"
#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/lldb-forward.h"
#include "llvm/ADT/DenseMap.h"

namespace lldb_private {
class TypeSystemCpp;
} // namespace lldb_private

namespace lldb_private {

/// DWARFASTParserCpp - DWARF parser that populates TypeSystemCpp with
/// LLDBTypeIR nodes.  No clang::* types are created here.
class DWARFASTParserCpp : public plugin::dwarf::DWARFASTParser {
public:
  explicit DWARFASTParserCpp(TypeSystemCpp &ast);
  ~DWARFASTParserCpp() override = default;

  // DWARFASTParser interface
  lldb::TypeSP ParseTypeFromDWARF(const SymbolContext &sc,
                                  const plugin::dwarf::DWARFDIE &die,
                                  bool *type_is_new_ptr) override;

  ConstString
  ConstructDemangledNameFromDWARF(const plugin::dwarf::DWARFDIE &die) override;

  Function *ParseFunctionFromDWARF(CompileUnit &comp_unit,
                                   const plugin::dwarf::DWARFDIE &die,
                                   AddressRanges ranges) override;

  bool CompleteTypeFromDWARF(const plugin::dwarf::DWARFDIE &die, Type *type,
                             const CompilerType &compiler_type) override;

  CompilerDecl
  GetDeclForUIDFromDWARF(const plugin::dwarf::DWARFDIE &die) override;

  CompilerDeclContext
  GetDeclContextForUIDFromDWARF(const plugin::dwarf::DWARFDIE &die) override;

  CompilerDeclContext GetDeclContextContainingUIDFromDWARF(
      const plugin::dwarf::DWARFDIE &die) override;

  void EnsureAllDIEsInDeclContextHaveBeenParsed(
      CompilerDeclContext decl_context) override;

  std::string
  GetDIEClassTemplateParams(plugin::dwarf::DWARFDIE die) override;

  /// Look up a nested typedef/using-declaration by name inside \p rec_node.
  /// Returns the LLDBTypeNode for the typedef if found (and parses it from
  /// DWARF if necessary), or nullptr if not found.
  LLDBTypeNode *FindNestedTypedefByName(LLDBTypeNode *rec_node,
                                        llvm::StringRef name);

private:
  TypeSystemCpp &m_ast;

  // DIE offset -> LLDBNamespaceNode* (namespace decl contexts)
  llvm::DenseMap<uint64_t, LLDBNamespaceNode *> m_die_to_namespace;
  // DIE offset -> LLDBTypeNode* (types)
  llvm::DenseMap<uint64_t, LLDBTypeNode *> m_die_to_type;
  // DIE offset -> lldb::TypeSP
  llvm::DenseMap<uint64_t, lldb::TypeSP> m_die_to_type_sp;
  // Reverse map: LLDBTypeNode* -> the DIE that defines it (set during
  // CompleteTypeFromDWARF so it points to the *complete* definition).
  llvm::DenseMap<LLDBTypeNode *, plugin::dwarf::DWARFDIE> m_type_to_die;

  // Namespace resolution
  LLDBNamespaceNode *ResolveNamespaceDIE(const plugin::dwarf::DWARFDIE &die);
  LLDBNamespaceNode *GetNamespaceForDIE(const plugin::dwarf::DWARFDIE &die);

  // Type resolution
  LLDBTypeNode *GetOrParseType(const plugin::dwarf::DWARFDIE &die);
  LLDBQualType GetQualTypeForDIE(const plugin::dwarf::DWARFDIE &die);
  /// Like GetOrParseType but peels DW_TAG_const/volatile/restrict wrappers
  /// and returns an LLDBQualType with qualifiers propagated.
  LLDBQualType GetOrParseQualType(const plugin::dwarf::DWARFDIE &die);

  // Helpers
  LLDBNamespaceNode *GetParentNamespace(const plugin::dwarf::DWARFDIE &die);
  CompilerDeclContext MakeNamespaceContext(LLDBNamespaceNode *ns);
};

} // namespace lldb_private

#endif // LLDB_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCPP_H
