//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCLIKE_H
#define LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCLIKE_H

#include "DWARFASTParser.h"
#include "DWARFDIE.h"

#include "lldb/Utility/ConstString.h"

#include <mutex>

namespace lldb_private {
class TypeSystemClike;
namespace clike {
class Type;
class RecordType;
class Builder;
} // namespace clike
} // namespace lldb_private

/// Parses DWARF debug info into the lldb-internal TypeSystemClike
/// representation.
///
/// This is the TypeSystemClike counterpart of DWARFASTParserClang. It only
/// knows how to build the small subset of the type system that TypeSystemClike
/// currently understands (base types and records).
class DWARFASTParserClike : public lldb_private::plugin::dwarf::DWARFASTParser {
public:
  DWARFASTParserClike(lldb_private::TypeSystemClike &ts);

  ~DWARFASTParserClike() override;

  // LLVM RTTI support
  static bool classof(const DWARFASTParser *Parser) {
    return Parser->GetKind() == Kind::DWARFASTParserClike;
  }

  // DWARFASTParser interface.
  lldb::TypeSP
  ParseTypeFromDWARF(const lldb_private::SymbolContext &sc,
                     const lldb_private::plugin::dwarf::DWARFDIE &die,
                     bool *type_is_new_ptr) override;

  lldb_private::ConstString ConstructDemangledNameFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  lldb_private::Function *
  ParseFunctionFromDWARF(lldb_private::CompileUnit &comp_unit,
                         const lldb_private::plugin::dwarf::DWARFDIE &die,
                         lldb_private::AddressRanges func_ranges) override;

  bool CompleteTypeFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die,
      lldb_private::Type *type,
      const lldb_private::CompilerType &compiler_type) override;

  lldb_private::CompilerDecl GetDeclForUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {
    return lldb_private::CompilerDecl();
  }

  lldb_private::CompilerDeclContext GetDeclContextForUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  lldb_private::CompilerDeclContext GetDeclContextContainingUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  void EnsureAllDIEsInDeclContextHaveBeenParsed(
      lldb_private::CompilerDeclContext decl_context) override {}

  std::string
  GetDIEClassTemplateParams(lldb_private::plugin::dwarf::DWARFDIE die) override;

  // TypeSystemClike doesn't import types from another AST, so it can't complete
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

  // TypeSystemClike doesn't track clang decl contexts.
  void *GetCachedDeclContext(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {
    return nullptr;
  }

  void LinkCachedDeclContextToDIE(
      void *decl_ctx,
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {}

private:
  /// TypeSystemClike understands DWARF 4 and later only.
  ///
  /// The older versions are not a subset: DWARF 2 and 3 spell things
  /// differently rather than less completely (no DW_AT_data_bit_offset, so
  /// bitfields are described by the DWARF 2 bit-offset triple that has to be
  /// read relative to the storage unit and byte order; DW_AT_bit_size on
  /// non-bitfields; no unified DW_AT_high_pc semantics; different reference
  /// forms). Every one of those would need its own translation, and reading
  /// such a unit as if it were DWARF 4 does not fail loudly -- it yields types
  /// that are quietly the wrong size or shape. Rejecting the unit means
  /// missing types, which is the honest answer and is easy to recognise.
  ///
  /// Returns false for a DIE from a unit older than DWARF 4, warning once per
  /// module so that the missing types have a stated reason.
  bool
  IsSupportedDWARFVersion(const lldb_private::plugin::dwarf::DWARFDIE &die);

  /// Warned about an unsupported DWARF version already; see
  /// IsSupportedDWARFVersion.
  std::once_flag m_unsupported_version_warning;

  lldb::TypeSP ParseBaseType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseUnspecifiedType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseStructureType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  /// If \p die is a forward declaration nested in a DW_TAG_module (a
  /// -gmodules/PCH container), find and return the complete definition of the
  /// type from the external module/PCH DWARF. Null if not applicable.
  lldb::TypeSP FindClangModuleDefinitionType(
      const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP ParseArrayType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParsePointerType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  /// Given the block-literal struct DIE of an Apple "blocks" pointer (the DIE
  /// carrying DW_AT_APPLE_block), return the block's function type by following
  /// its `__FuncPtr` member. Returns an invalid CompilerType if the struct
  /// doesn't have the expected shape.
  lldb_private::CompilerType
  ParseBlockFunctionType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP ParseTypedef(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseCVQualifiedType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParsePtrAuthType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseAtomicType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP ParseEnum(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseFunctionType(const lldb_private::plugin::dwarf::DWARFDIE &die);

  /// Resolve a DWARF type reference to its TypeSystemClike node. This is the
  /// unit of work spread across threads while completing a record: it may run
  /// on a worker, so it serializes its access to the shared type system through
  /// TypeSystemClike's lock. Returns nullptr if the reference can't be
  /// resolved.
  lldb_private::clike::Type *ResolveReferencedType(
      const lldb_private::plugin::dwarf::DWARFDIE &referencing_die,
      const lldb_private::plugin::dwarf::DWARFDIE &type_die);

  lldb_private::TypeSystemClike &m_ts;
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCLIKE_H
