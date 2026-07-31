//===-- DWARFASTParserClike.h -------------------------------------*- C++ -*-===//
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

#include "llvm/ADT/DenseMap.h"

#include <utility>
#include <vector>

namespace lldb_private {
class TypeSystemClike;
namespace clike_typesystem {
class Type;
class RecordType;
class ObjCInterfaceType;
class Builder;
}
} // namespace lldb_private

/// Parses DWARF debug info into the lldb-internal TypeSystemClike representation.
///
/// This is the TypeSystemClike counterpart of DWARFASTParserClang. It only knows
/// how to build the small subset of the type system that TypeSystemClike
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
      const lldb_private::plugin::dwarf::DWARFDIE &die, lldb_private::Type *type,
      const lldb_private::CompilerType &compiler_type) override;

  /// Parse the member functions of \p record, which must have been completed
  /// earlier via CompleteTypeFromDWARF. Member functions are parsed lazily
  /// (only when the expression evaluator needs them to call a method) rather
  /// than as part of record completion, so this is a separate on-demand step
  /// reached through TypeSystemClike::CompleteMemberFunctions.
  void CompleteMemberFunctionsFromDWARF(
      lldb_private::clike_typesystem::RecordType &record);

  /// Parse the Objective-C methods of \p iface (from its child method
  /// declarations and the standalone `+[Class sel]` / `-[Class sel]`
  /// definitions reachable via the ObjC-method index) and add them as
  /// ObjCMethods so the expression parser can message-send them. Called from
  /// CompleteMemberFunctionsFromDWARF for an ObjCInterfaceType.
  void CompleteObjCMethodsFromDWARF(
      lldb_private::clike_typesystem::ObjCInterfaceType &iface,
      const lldb_private::plugin::dwarf::DWARFDIE &class_die,
      lldb_private::clike_typesystem::Builder &ts);

  lldb_private::CompilerDecl GetDeclForUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override {
    return lldb_private::CompilerDecl();
  }

  lldb_private::CompilerDeclContext GetDeclContextForUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  lldb_private::CompilerDeclContext GetDeclContextContainingUIDFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &die) override;

  /// Collect the target namespaces of the `using namespace` directives
  /// (DW_TAG_imported_module) lexically in scope at \p block_die and its
  /// enclosing blocks, innermost-first. Used so an expression evaluated in a
  /// block with an active using-directive resolves unqualified names through
  /// the imported namespace.
  void CollectUsingDirectiveNamespaces(
      const lldb_private::plugin::dwarf::DWARFDIE &block_die,
      std::vector<lldb_private::CompilerDeclContext> &namespaces);

  /// Collect the `using` *declarations* (DW_TAG_imported_declaration, e.g.
  /// `using Single::single;`) lexically in scope at \p block_die and its
  /// enclosing blocks, innermost-first. Each is reported as the imported
  /// unqualified name paired with the namespace it names the entity in, so an
  /// expression evaluated in that scope resolves the imported name through that
  /// namespace.
  void CollectUsingDeclarations(
      const lldb_private::plugin::dwarf::DWARFDIE &block_die,
      std::vector<std::pair<lldb_private::ConstString,
                            lldb_private::CompilerDeclContext>> &decls);

  /// If \p function_die is a C++ member function (its semantic parent is a
  /// class/struct/union), return the owning class' CompilerType; otherwise an
  /// invalid CompilerType. Works for static member functions too, which have no
  /// `this` pointer to derive the class from.
  lldb_private::CompilerType GetOwningClassForFunctionFromDWARF(
      const lldb_private::plugin::dwarf::DWARFDIE &function_die);

  void EnsureAllDIEsInDeclContextHaveBeenParsed(
      lldb_private::CompilerDeclContext decl_context) override {}

  std::string GetDIEClassTemplateParams(
      lldb_private::plugin::dwarf::DWARFDIE die) override;

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
  lldb::TypeSP
  ParseBaseType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseUnspecifiedType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseStructureType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  /// If \p die is a forward declaration nested in a DW_TAG_module (a
  /// -gmodules/PCH container), find and return the complete definition of the
  /// type from the external module/PCH DWARF. Null if not applicable.
  lldb::TypeSP
  FindClangModuleDefinitionType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseArrayType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParsePointerType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  /// Given the block-literal struct DIE of an Apple "blocks" pointer (the DIE
  /// carrying DW_AT_APPLE_block), return the block's function type by following
  /// its `__FuncPtr` member. Returns an invalid CompilerType if the struct
  /// doesn't have the expected shape.
  lldb_private::CompilerType
  ParseBlockFunctionType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseReferenceType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParsePointerToMemberType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseTypedef(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseCVQualifiedType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParsePtrAuthType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseAtomicType(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP ParseEnum(const lldb_private::plugin::dwarf::DWARFDIE &die);
  lldb::TypeSP
  ParseFunctionType(const lldb_private::plugin::dwarf::DWARFDIE &die);

  /// Resolve a DWARF type reference to its TypeSystemClike node. This is the unit
  /// of work spread across threads while completing a record: it may run on a
  /// worker, so it serializes its access to the shared type system through
  /// TypeSystemClike's lock. Returns nullptr if the reference can't be resolved.
  lldb_private::clike_typesystem::Type *ResolveReferencedType(
      const lldb_private::plugin::dwarf::DWARFDIE &referencing_die,
      const lldb_private::plugin::dwarf::DWARFDIE &type_die);

  lldb_private::TypeSystemClike &m_ts;

  /// Maps each record that was completed via CompleteTypeFromDWARF to its
  /// defining DIE, so that CompleteMemberFunctionsFromDWARF can re-find the DIE
  /// to parse the record's member functions on demand (the forward-declaration
  /// map in SymbolFileDWARF is erased once completion starts, so it can't be
  /// reused for this). Populated during completion.
  llvm::DenseMap<lldb_private::clike_typesystem::Type *,
                 lldb_private::plugin::dwarf::DWARFDIE>
      m_record_defining_die;
};

#endif // LLDB_SOURCE_PLUGINS_SYMBOLFILE_DWARF_DWARFASTPARSERCLIKE_H
