//===-- TypeSystemCpp.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// TypeSystemCpp — LLDB's Clang-free C++ type system.
///
/// ARCHITECTURAL RULES — READ BEFORE MODIFYING:
///  1. TypeSystemCpp must NEVER create or hold any clang::* objects.
///  2. TypeSystemCpp must NEVER use TypeSystemClang internally.
///  3. All types are stored as LLDBTypeIR nodes owned by m_registry.
///  4. Namespace/decl-context info is stored in LLDBNamespaceNode objects.
///  5. Expression evaluation uses ClangASTGenerator to translate LLDBTypeIR
///     → Clang AST on demand (in the expression parser layer, NOT here).
///  6. opaque_compiler_type_t = LLDBTypeNode* (lower 3 bits encode CVR quals).
///  7. opaque_decl_ctx = LLDBNamespaceNode* (nullptr = translation-unit level).
///
//===----------------------------------------------------------------------===//

#ifndef LLDB_PLUGINS_TYPESYSTEM_CLANG_TYPESYSTEMCPP_H
#define LLDB_PLUGINS_TYPESYSTEM_CLANG_TYPESYSTEMCPP_H

#include <memory>

#include "LLDBTypeIR.h"
#include "TypeSystemClang.h"
#include "llvm/ADT/DenseMap.h"

namespace lldb_private {

class DWARFASTParserCpp;

/// TypeSystemCpp — LLDB's Clang-free C++ type system.
///
/// ARCHITECTURAL RULES — READ BEFORE MODIFYING:
///
///  1. This class must NEVER create or hold any clang::* objects
///     (clang::Decl, clang::Type, clang::ASTContext, …).
///  2. This class must NEVER include or call into TypeSystemClang.
///  3. All type and declaration information is stored in LLDBTypeIR nodes
///     owned by m_registry (LLDBTypeRegistry).
///  4. Namespace and decl-context information is stored in LLDBNamespaceNode
///     objects owned by m_registry.
///  5. The expression parser still uses Clang, but TypeSystemCpp is kept
///     completely free of Clang.  The ClangASTGenerator (in
///     Plugins/ExpressionParser/Clang/) translates LLDBTypeIR nodes into
///     Clang AST nodes on demand for expression evaluation.
///  6. opaque_compiler_type_t for this TypeSystem is always a
///     LLDBTypeNode* with CVR qualifiers encoded in the lowest 3 bits.
///  7. opaque_decl_context for this TypeSystem is always a
///     LLDBNamespaceNode* (nullptr = translation-unit level).
class TypeSystemCpp : public TypeSystemCBase {
  static char ID;

public:
  ~TypeSystemCpp() override;

  /// Factory — constructs and initialises a TypeSystemCpp.
  static std::shared_ptr<TypeSystemCpp> Create(llvm::StringRef name,
                                               llvm::Triple triple);

  // LLVM RTTI support
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  // PluginInterface
  llvm::StringRef GetPluginName() override { return "cpp"; }
  static llvm::StringRef GetPluginNameStatic() { return "cpp"; }

  void Finalize() override;

  // DWARFParser: provided by DWARFASTParserCpp.
  plugin::dwarf::DWARFASTParser *GetDWARFParser() override;

  /// Called by DWARFASTParserCpp when it has the complete definition DIE for
  /// a record node.  Used to route FindNestedTypedefByName to the right parser
  /// even when the type was first loaded from a different compilation unit.
  void SetCompleteParserForNode(LLDBTypeNode *node, DWARFASTParserCpp *parser) {
    m_node_to_complete_parser[node] = parser;
  }
  DWARFASTParserCpp *GetCompleteParserForNode(LLDBTypeNode *node) const {
    auto it = m_node_to_complete_parser.find(node);
    return it != m_node_to_complete_parser.end() ? it->second : nullptr;
  }
  PDBASTParser *GetPDBParser() override;
  npdb::PdbAstBuilder *GetNativePDBParser() override;

  void SetSymbolFile(SymbolFile *sym_file) override;

  LLDBTypeRegistry &GetTypeRegistry() { return m_registry; }
  llvm::Triple GetTriple() const { return m_triple; }

  /// Extract the raw LLDBTypeNode* from a CompilerType backed by TypeSystemCpp.
  /// Returns nullptr if the type is not from TypeSystemCpp.
  static LLDBTypeNode *GetNode(const CompilerType &type);
  /// Extract the CVR qualifiers from a CompilerType backed by TypeSystemCpp.
  static LLDBQualifiers GetQuals(const CompilerType &type);

  /// Create a CompilerType for the given LLDBQualType node.
  CompilerType MakeCompilerType(LLDBQualType qt);

  // --- CompilerDecl functions ---
  ConstString DeclGetName(void *opaque_decl) override;
  ConstString DeclGetMangledName(void *opaque_decl) override;
  CompilerDeclContext DeclGetDeclContext(void *opaque_decl) override;
  CompilerType DeclGetFunctionReturnType(void *opaque_decl) override;
  size_t DeclGetFunctionNumArguments(void *opaque_decl) override;
  CompilerType DeclGetFunctionArgumentType(void *opaque_decl,
                                           size_t arg_idx) override;
  std::vector<lldb_private::CompilerContext>
  DeclGetCompilerContext(void *opaque_decl) override;
  Scalar DeclGetConstantValue(void *opaque_decl) override;
  CompilerType GetTypeForDecl(void *opaque_decl) override;

  // --- CompilerDeclContext functions ---
  std::vector<CompilerDecl>
  DeclContextFindDeclByName(void *opaque_decl_ctx, ConstString name,
                            const bool ignore_using_decls) override;
  ConstString DeclContextGetName(void *opaque_decl_ctx) override;
  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override;
  bool DeclContextIsClassMethod(void *opaque_decl_ctx) override;
  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override;
  lldb::LanguageType DeclContextGetLanguage(void *opaque_decl_ctx) override;
  std::vector<lldb_private::CompilerContext>
  DeclContextGetCompilerContext(void *opaque_decl_ctx) override;

  CompilerDeclContext
  GetCompilerDeclContextForType(const CompilerType &type) override;

  // --- Tests ---
#ifndef NDEBUG
  bool Verify(lldb::opaque_compiler_type_t type) override;
#endif

  bool IsArrayType(lldb::opaque_compiler_type_t type, CompilerType *element_type,
                   uint64_t *size, bool *is_incomplete) override;
  bool IsVectorType(lldb::opaque_compiler_type_t type,
                    CompilerType *element_type, uint64_t *size) override;
  bool IsAggregateType(lldb::opaque_compiler_type_t type) override;
  bool IsAnonymousType(lldb::opaque_compiler_type_t type) override;
  bool IsBeingDefined(lldb::opaque_compiler_type_t type) override;
  bool IsCharType(lldb::opaque_compiler_type_t type) override;
  bool IsCompleteType(lldb::opaque_compiler_type_t type) override;
  bool IsConst(lldb::opaque_compiler_type_t type) override;
  bool IsDefined(lldb::opaque_compiler_type_t type) override;
  bool IsFloatingPointType(lldb::opaque_compiler_type_t type) override;
  bool IsFunctionType(lldb::opaque_compiler_type_t type) override;
  uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                  CompilerType *base_type_ptr) override;
  size_t GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          const size_t index) override;
  bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsMemberFunctionPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsMemberDataPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                          CompilerType *function_pointer_type_ptr) override;
  bool IsIntegerType(lldb::opaque_compiler_type_t type,
                     bool &is_signed) override;
  bool IsEnumerationType(lldb::opaque_compiler_type_t type,
                         bool &is_signed) override;
  bool IsScopedEnumerationType(lldb::opaque_compiler_type_t type) override;
  bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) override;
  bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                             CompilerType *target_type, bool check_cplusplus,
                             bool check_objc) override;
  bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) override;
  bool IsPointerType(lldb::opaque_compiler_type_t type,
                     CompilerType *pointee_type) override;
  bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                CompilerType *pointee_type) override;
  bool IsReferenceType(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_type, bool *is_rvalue) override;
  bool IsScalarType(lldb::opaque_compiler_type_t type) override;
  bool IsTypedefType(lldb::opaque_compiler_type_t type) override;
  bool IsVoidType(lldb::opaque_compiler_type_t type) override;
  bool HasPointerAuthQualifier(lldb::opaque_compiler_type_t type) override;
  bool CanPassInRegisters(const CompilerType &type) override;
  bool SupportsLanguage(lldb::LanguageType language) override;
  bool IsForcefullyCompleted(lldb::opaque_compiler_type_t type) override;

  // --- Type completion ---
  bool GetCompleteType(lldb::opaque_compiler_type_t type) override;

  // --- AST-related queries ---
  uint32_t GetPointerByteSize() override;
  CompilerType GetPointerDiffType(bool is_signed) override;
  unsigned GetPtrAuthKey(lldb::opaque_compiler_type_t type) override;
  unsigned GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t type) override;
  bool GetPtrAuthAddressDiversity(lldb::opaque_compiler_type_t type) override;

  // --- Accessors ---
  ConstString GetTypeName(lldb::opaque_compiler_type_t type,
                          bool base_only) override;
  ConstString GetDisplayTypeName(lldb::opaque_compiler_type_t type) override;
  uint32_t GetTypeInfo(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_or_element_compiler_type) override;
  lldb::LanguageType GetMinimumLanguage(lldb::opaque_compiler_type_t type) override;
  lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) override;
  unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) override;

  // --- Creating related types ---
  CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) override;
  CompilerType GetArrayType(lldb::opaque_compiler_type_t type,
                            uint64_t size) override;
  CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) override;
  int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx) override;
  CompilerType GetFunctionReturnType(lldb::opaque_compiler_type_t type) override;
  size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) override;
  TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                           size_t idx) override;
  CompilerType GetNonReferenceType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetPointerType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetLValueReferenceType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetRValueReferenceType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetAtomicType(lldb::opaque_compiler_type_t type) override;
  CompilerType AddConstModifier(lldb::opaque_compiler_type_t type) override;
  CompilerType AddVolatileModifier(lldb::opaque_compiler_type_t type) override;
  CompilerType AddRestrictModifier(lldb::opaque_compiler_type_t type) override;
  CompilerType AddPtrAuthModifier(lldb::opaque_compiler_type_t type,
                                  uint32_t payload) override;
  CompilerType CreateTypedef(lldb::opaque_compiler_type_t type, const char *name,
                             const CompilerDeclContext &decl_ctx,
                             uint32_t opaque_payload) override;
  CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override;
  CompilerType
  GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                      size_t bit_size) override;
  CompilerType GetBuiltinTypeByName(ConstString name) override;
  CompilerType CreateGenericFunctionPrototype() override;
  CompilerType GetTypeForFormatters(void *type) override;

  // --- Exploring the type ---
  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size,
                                                  lldb::Format format) override;
  llvm::Expected<uint64_t>
  GetBitSize(lldb::opaque_compiler_type_t type,
             ExecutionContextScope *exe_scope) override;
  lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type) override;
  lldb::Format GetFormat(lldb::opaque_compiler_type_t type) override;
  std::optional<size_t>
  GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                  ExecutionContextScope *exe_scope) override;
  llvm::Expected<uint32_t>
  GetNumChildren(lldb::opaque_compiler_type_t type,
                 bool omit_empty_base_classes,
                 const ExecutionContext *exe_ctx) override;
  lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) override;
  void ForEachEnumerator(
      lldb::opaque_compiler_type_t type,
      std::function<bool(const CompilerType &integer_type, ConstString name,
                         const llvm::APSInt &value)> const &callback) override;
  uint32_t GetNumFields(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                               std::string &name, uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override;
  uint32_t GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) override;
  uint32_t GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) override;
  CompilerType GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset_ptr) override;
  CompilerType GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset_ptr) override;
  CompilerDecl GetStaticFieldWithName(lldb::opaque_compiler_type_t type,
                                      llvm::StringRef name) override;
  llvm::Expected<CompilerType>
  GetDereferencedType(lldb::opaque_compiler_type_t type,
                      ExecutionContext *exe_ctx, std::string &deref_name,
                      uint32_t &deref_byte_size, int32_t &deref_byte_offset,
                      ValueObject *valobj, uint64_t &language_flags) override;
  llvm::Expected<CompilerType> GetChildCompilerTypeAtIndex(
      lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override;
  llvm::Expected<uint32_t>
  GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                          llvm::StringRef name,
                          bool omit_empty_base_classes) override;
  size_t
  GetIndexOfChildMemberWithName(lldb::opaque_compiler_type_t type,
                                llvm::StringRef name,
                                bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) override;
  CompilerType GetDirectNestedTypeWithName(lldb::opaque_compiler_type_t type,
                                           llvm::StringRef name) override;
  bool IsTemplateType(lldb::opaque_compiler_type_t type) override;
  size_t GetNumTemplateArguments(lldb::opaque_compiler_type_t type,
                                 bool expand_pack) override;
  lldb::TemplateArgumentKind
  GetTemplateArgumentKind(lldb::opaque_compiler_type_t type, size_t idx,
                          bool expand_pack) override;
  CompilerType GetTypeTemplateArgument(lldb::opaque_compiler_type_t type,
                                       size_t idx, bool expand_pack) override;
  std::optional<CompilerType::IntegralTemplateArgument>
  GetIntegralTemplateArgument(lldb::opaque_compiler_type_t type, size_t idx,
                              bool expand_pack) override;
  bool IsPromotableIntegerType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetPromotedIntegerType(lldb::opaque_compiler_type_t type) override;

  // --- Dumping ---
#ifndef NDEBUG
  LLVM_DUMP_METHOD void dump(lldb::opaque_compiler_type_t type) const override;
#endif
  bool DumpTypeValue(lldb::opaque_compiler_type_t type, Stream &s,
                     lldb::Format format, const DataExtractor &data,
                     lldb::offset_t data_offset, size_t data_byte_size,
                     uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope) override;
  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type,
      lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) override;
  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type, Stream &s,
      lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) override;
  void Dump(llvm::raw_ostream &output, llvm::StringRef filter,
            bool show_color) override;

protected:
  explicit TypeSystemCpp(llvm::StringRef name, llvm::Triple triple);

private:
  llvm::Triple m_triple;
  LLDBTypeRegistry m_registry;
  std::unique_ptr<DWARFASTParserCpp> m_dwarf_parser;
  llvm::DenseMap<LLDBPointerTypeNode *, LLDBPointerTypeNode *>
      m_unqual_ptr_map;
  /// Maps record type nodes to the DWARFASTParserCpp that has their complete
  /// definition DIE.  Used by GetDirectNestedTypeWithName to find nested
  /// typedefs across modules (e.g., a forward-declared type completed from a
  /// different compilation unit's parser).
  llvm::DenseMap<LLDBTypeNode *, DWARFASTParserCpp *> m_node_to_complete_parser;

  /// Ensure the derived type qt lives in THIS registry's type graph by finding
  /// a structurally-equivalent node already in the registry (or creating one).
  /// Non-derived nodes (Record, Builtin, Enum, …) are returned as-is since
  /// they are shared by pointer across registries.
  LLDBQualType AdoptQualType(LLDBQualType qt);

public:
  /// After a record's DWARF fields have been parsed, synthesize unnamed
  /// bitfields for any gaps that were not emitted to DWARF. This mirrors the
  /// logic in DWARFASTParserClang::AddUnnamedBitfieldToRecordTypeIfNeeded.
  void SynthesizeUnnamedBitfields(LLDBRecordTypeNode *rec);
};

/// ScratchTypeSystemCpp — the scratch (target-level) TypeSystem for cpp mode.
///
/// Inherits all type-representation methods from TypeSystemCpp (LLDBTypeIR).
/// The inner m_scratch_clang is used ONLY for expression evaluation
/// infrastructure (GetUserExpression, GetPersistentExpressionState, etc.).
/// Types created during expression evaluation live in m_scratch_clang and
/// are NOT routed through ScratchTypeSystemCpp's type representation.
class ScratchTypeSystemCpp : public TypeSystemCpp {
  static char ID;

public:
  ScratchTypeSystemCpp(Target &target, llvm::Triple triple);
  ~ScratchTypeSystemCpp() override;

  // LLVM RTTI: supports both ScratchTypeSystemCpp and TypeSystemCpp queries.
  bool isA(const void *ClassID) const override {
    return ClassID == &ID || TypeSystemCpp::isA(ClassID);
  }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  // PluginInterface
  llvm::StringRef GetPluginName() override { return "cpp"; }
  static llvm::StringRef GetPluginNameStatic() { return "cpp"; }

  void Finalize() override;
  void SetSymbolFile(SymbolFile *sym_file) override;

  // Access to the inner ScratchTypeSystemClang used by
  // ScratchTypeSystemClang::GetForTarget so the expression evaluator can find
  // its Clang scratch AST.
  std::shared_ptr<ScratchTypeSystemClang> GetScratchClangSP() const {
    return m_scratch_clang;
  }

  // Expression evaluation — delegated to the inner ScratchTypeSystemClang.
  UserExpression *GetUserExpression(llvm::StringRef expr,
                                    llvm::StringRef prefix,
                                    SourceLanguage language,
                                    Expression::ResultType desired_type,
                                    const EvaluateExpressionOptions &options,
                                    ValueObject *ctx_obj) override;
  FunctionCaller *GetFunctionCaller(const CompilerType &return_type,
                                    const Address &function_address,
                                    const ValueList &arg_value_list,
                                    const char *name) override;
  std::unique_ptr<UtilityFunction>
  CreateUtilityFunction(std::string text, std::string name) override;
  PersistentExpressionState *GetPersistentExpressionState() override;

private:
  std::shared_ptr<ScratchTypeSystemClang> m_scratch_clang;
};

} // namespace lldb_private

#endif // LLDB_PLUGINS_TYPESYSTEM_CLANG_TYPESYSTEMCPP_H
