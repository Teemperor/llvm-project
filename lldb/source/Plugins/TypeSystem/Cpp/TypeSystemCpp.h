//===-- TypeSystemCpp.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPESYSTEMCPP_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPESYSTEMCPP_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/TargetParser/Triple.h"

#include "Builder.h"
#include "Context.h"

#include <memory>
#include <mutex>

class DWARFASTParserCpp;

namespace lldb_private {

class ObjCLanguageRuntime;

class TypeSystemCpp : public TypeSystem {
  // LLVM RTTI support
  static char ID;

public:
  TypeSystemCpp(llvm::StringRef name, llvm::Triple triple);
  ~TypeSystemCpp() override;

  static lldb::TypeSystemSP Create(llvm::StringRef name, llvm::Triple triple);

  // DWARF parsing
  plugin::dwarf::DWARFASTParser *GetDWARFParser() override;

  /// The target namespaces of the `using namespace` directives lexically in
  /// scope at \p block (innermost first). Used by the expression evaluator so
  /// an unqualified name is resolved through an active using-directive. Empty
  /// unless this type system was populated from DWARF.
  std::vector<CompilerDeclContext>
  GetUsingDirectiveNamespaces(Block &block);

  /// The `using` declarations (e.g. `using Single::single;`) lexically in scope
  /// at \p block (innermost first), each reported as the imported unqualified
  /// name paired with the namespace it names the entity in. Used by the
  /// expression evaluator so an unqualified name brought in by a using
  /// declaration resolves to that namespace's entity. Empty unless this type
  /// system was populated from DWARF.
  std::vector<std::pair<ConstString, CompilerDeclContext>>
  GetUsingDeclarations(Block &block);

  /// If \p function_block is the block of a C++ member function (including a
  /// static member function, which has no `this`), return the owning class'
  /// CompilerType; otherwise an invalid CompilerType. Used by the expression
  /// evaluator to establish the `$__lldb_class` context for unqualified member
  /// lookups even when there is no `this` pointer. Empty unless this type
  /// system was populated from DWARF.
  CompilerType GetOwningClassForFunction(Block &function_block);

  /// Wrap one of our own Type nodes into a CompilerType owned by this system.
  CompilerType GetCompilerType(cpp_typesystem::Type *type);  /// Recover the Type node backing a CompilerType created by this system.
  static cpp_typesystem::Type *GetCppType(lldb::opaque_compiler_type_t type) {
    return static_cast<cpp_typesystem::Type *>(type);
  }

  /// The target triple this type system was created for (used e.g. to build a
  /// throwaway Clang AST for ABI/vtable-layout queries).
  const llvm::Triple &GetTriple() const { return m_triple; }

  // Plugin lifecycle
  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "cpp"; }

  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Module *module, Target *target);

  static LanguageSet GetSupportedLanguagesForTypes();
  static LanguageSet GetSupportedLanguagesForExpressions();

  // PluginInterface
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // LLVM RTTI support
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  // CompilerDecl functions
  ConstString DeclGetName(void *opaque_decl) override;
  ConstString DeclGetMangledName(void *opaque_decl) override;
  CompilerType GetTypeForDecl(void *opaque_decl) override;
  Scalar DeclGetConstantValue(void *opaque_decl) override;

  // CompilerDeclContext functions
  ConstString DeclContextGetName(void *opaque_decl_ctx) override;
  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override;
  std::vector<lldb_private::CompilerContext>
  DeclContextGetCompilerContext(void *opaque_decl_ctx) override;
  bool DeclContextIsClassMethod(void *opaque_decl_ctx) override;
  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override;
  lldb::LanguageType DeclContextGetLanguage(void *opaque_decl_ctx) override;

  // Tests
#ifndef NDEBUG
  bool Verify(lldb::opaque_compiler_type_t type) override;
#endif

  bool IsArrayType(lldb::opaque_compiler_type_t type,
                   CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override;
  bool IsAggregateType(lldb::opaque_compiler_type_t type) override;
  bool IsAnonymousType(lldb::opaque_compiler_type_t type) override;
  bool IsCharType(lldb::opaque_compiler_type_t type) override;
  bool IsCompleteType(lldb::opaque_compiler_type_t type) override;
  bool IsDefined(lldb::opaque_compiler_type_t type) override;
  bool IsFloatingPointType(lldb::opaque_compiler_type_t type) override;
  bool IsFunctionType(lldb::opaque_compiler_type_t type) override;
  size_t
  GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          const size_t index) override;
  bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsMemberFunctionPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsMemberDataPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                          CompilerType *function_pointer_type_ptr) override;
  bool IsIntegerType(lldb::opaque_compiler_type_t type,
                     bool &is_signed) override;
  bool IsScopedEnumerationType(lldb::opaque_compiler_type_t type) override;
  bool IsEnumerationType(lldb::opaque_compiler_type_t type,
                         bool &is_signed) override;
  bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                             CompilerType *target_type, bool check_cplusplus,
                             bool check_objc) override;
  bool IsPointerType(lldb::opaque_compiler_type_t type,
                     CompilerType *pointee_type) override;
  bool IsScalarType(lldb::opaque_compiler_type_t type) override;
  bool IsVoidType(lldb::opaque_compiler_type_t type) override;
  bool CanPassInRegisters(const CompilerType &type) override;
  bool SupportsLanguage(lldb::LanguageType language) override;

  // Type Completion
  bool GetCompleteType(lldb::opaque_compiler_type_t type) override;
  bool IsForcefullyCompleted(lldb::opaque_compiler_type_t type) override;

  /// Parse \p type's member functions from debug info if they haven't been
  /// already. Member functions are only needed by the expression evaluator (to
  /// call methods), so -- unlike fields and base classes -- they are not parsed
  /// as part of GetCompleteType. This is the deferred step that fills them in
  /// on demand (see ClangASTGenerator::PopulateRecord).
  void CompleteMemberFunctions(cpp_typesystem::Type *type);

  /// Complete an incomplete class-template instantiation so its modeled
  /// template arguments are available for building its display name (see the
  /// definition for details). A no-op for non-record or already-complete types.
  void CompleteTemplateInstantiationForName(cpp_typesystem::Type *type);

  // AST related queries
  uint32_t GetPointerByteSize() override;
  CompilerType GetPointerDiffType(bool is_signed) override;
  unsigned GetPtrAuthKey(lldb::opaque_compiler_type_t type) override;
  unsigned GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t type) override;
  bool GetPtrAuthAddressDiversity(lldb::opaque_compiler_type_t type) override;
  bool HasPointerAuthQualifier(lldb::opaque_compiler_type_t type) override;

  // Accessors
  ConstString GetTypeName(lldb::opaque_compiler_type_t type,
                          bool BaseOnly) override;
  ConstString GetDisplayTypeName(lldb::opaque_compiler_type_t type) override;
  uint32_t GetTypeInfo(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_or_element_compiler_type) override;
  lldb::LanguageType
  GetMinimumLanguage(lldb::opaque_compiler_type_t type) override;
  lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) override;

  // Creating related types
  CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) override;
  CompilerType GetArrayType(lldb::opaque_compiler_type_t type,
                            uint64_t size) override;
  CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) override;
  CompilerType
  GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) override;
  void ForEachEnumerator(
      lldb::opaque_compiler_type_t type,
      std::function<bool(const CompilerType &integer_type, ConstString name,
                         const llvm::APSInt &value)> const &callback) override;
  int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx) override;
  CompilerType
  GetFunctionReturnType(lldb::opaque_compiler_type_t type) override;
  size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) override;
  TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                           size_t idx) override;
  CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetPointerType(lldb::opaque_compiler_type_t type) override;
  CompilerType
  GetLValueReferenceType(lldb::opaque_compiler_type_t type) override;
  CompilerType
  GetRValueReferenceType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetTypeForFormatters(lldb::opaque_compiler_type_t type) override;

  // Exploring the type
  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size,
                                                  lldb::Format format) override;
  llvm::Expected<uint64_t>
  GetBitSize(lldb::opaque_compiler_type_t type,
             ExecutionContextScope *exe_scope) override;
  lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type) override;
  lldb::Format GetFormat(lldb::opaque_compiler_type_t type) override;
  llvm::Expected<uint32_t>
  GetNumChildren(lldb::opaque_compiler_type_t type,
                 bool omit_empty_base_classes,
                 const ExecutionContext *exe_ctx) override;
  lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) override;
  bool IsPromotableIntegerType(lldb::opaque_compiler_type_t type) override;
  CompilerType
  GetPromotedIntegerType(lldb::opaque_compiler_type_t type) override;
  uint32_t GetNumFields(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                               std::string &name, uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override;
  CompilerDecl GetStaticFieldWithName(lldb::opaque_compiler_type_t type,
                                      llvm::StringRef name) override;
  uint32_t GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) override;
  uint32_t GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) override;
  CompilerType GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset_ptr) override;
  CompilerType GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset_ptr) override;
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

  // Dumping types
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

  bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) override;

  bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                CompilerType *pointee_type) override;
  unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) override;
  std::optional<size_t>
  GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                  ExecutionContextScope *exe_scope) override;
  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override;
  CompilerType CreateGenericFunctionPrototype() override;
  CompilerType GetBuiltinTypeByName(ConstString name) override;
  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override;
  bool IsBeingDefined(lldb::opaque_compiler_type_t type) override;
  bool IsConst(lldb::opaque_compiler_type_t type) override;
  uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                  CompilerType *base_type_ptr) override;
  bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) override;
  bool IsTypedefType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) override;
  bool IsVectorType(lldb::opaque_compiler_type_t type,
                    CompilerType *element_type, uint64_t *size) override;
  CompilerType
  GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetNonReferenceType(lldb::opaque_compiler_type_t type) override;
  bool IsReferenceType(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_type, bool *is_rvalue) override;

  // Template argument access (used by e.g. data formatters to recover a
  // container's element type).
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
  CompilerType GetDirectNestedTypeWithName(lldb::opaque_compiler_type_t type,
                                           llvm::StringRef name) override;

  /// Get-or-create (in this scratch context) an ObjCInterfaceType for
  /// \p class_name whose ivars are populated from the ObjC runtime. Exposed
  /// publicly (beyond the frame-variable/DIL path, which reaches this via the
  /// private GetRuntimeCompletedObjCType) for CppExpressionDeclMap::LookupType,
  /// which needs to resolve a bare Objective-C class name that has NO debug
  /// info at all (e.g. an @implementation compiled with -g0): there is no
  /// cpp_typesystem::Type to redirect from in that case, only a name.
  CompilerType CreateRuntimeObjCInterface(ConstString class_name,
                                          Process &process,
                                          ObjCLanguageRuntime &runtime);

private:
  friend class cpp_typesystem::Builder;

  // Recursive worker for GetIndexOfChildMemberWithName. `descend_anon_fields`
  // controls whether an unnamed (anonymous union/struct) field is transparently
  // searched: it is true only for the record the lookup starts from, and false
  // when recursing into a base class. This mirrors C++ name lookup (and
  // TypeSystemClang): an anonymous field injects its members into its immediately
  // enclosing record, so they are reachable directly, but that injection does not
  // propagate through a further base class -- i.e. `derived.member` does not find
  // a member that lives in an anonymous field of one of `derived`'s bases.
  size_t GetIndexOfChildMemberWithNameImpl(
      lldb::opaque_compiler_type_t type, llvm::StringRef name,
      bool omit_empty_base_classes, bool descend_anon_fields,
      std::vector<uint32_t> &child_indexes);

  /// If \p type is a pointer to an Objective-C interface, return the (completed)
  /// interface type; otherwise the desugared \p type. ObjC objects are always
  /// referenced by pointer, so base-class queries on `Foo *` are answered by the
  /// interface `Foo`.
  cpp_typesystem::Type *
  GetObjCBaseClassBearingType(lldb::opaque_compiler_type_t type);

  /// Recursive worker for GetFullyUnqualifiedType: strip top-level
  /// cv-qualifiers and recurse through pointer/reference/array pointees so the
  /// whole type is cv-unqualified (see the definition for details).
  cpp_typesystem::Type *
  GetFullyUnqualifiedTypeImpl(cpp_typesystem::Type *type);

  /// For an Objective-C interface whose ivars are NOT in the debug info (e.g.
  /// ivars declared in an @implementation compiled with -g0), build the ivar
  /// list from the ObjC runtime. To keep process-specific runtime data out of
  /// the shared per-module types, the completed type is created in the target's
  /// **scratch** TypeSystemCpp; \p t (a module type with no fields) is left
  /// untouched. Returns the scratch CompilerType, or an empty one when no
  /// redirection applies (no process/runtime, not an ObjC interface, the type
  /// already has fields, or this already is the scratch context).
  CompilerType GetRuntimeCompletedObjCType(cpp_typesystem::Type *t,
                                           const ExecutionContext *exe_ctx);
  std::string m_display_name;
  llvm::Triple m_triple;
  cpp_typesystem::Context m_context;
  std::unique_ptr<DWARFASTParserCpp> m_dwarf_ast_parser_up;

  /// ObjC interfaces whose ivars were built from the runtime, keyed by class
  /// name. Only populated on a scratch context (see CreateRuntimeObjCInterface),
  /// so process-specific runtime layout never leaks into a shared module type.
  llvm::StringMap<cpp_typesystem::Type *> m_runtime_objc_types;

  /// The most recent live process seen through an ExecutionContext (e.g. in
  /// GetChildCompilerTypeAtIndex/GetNumChildren). Used as a fallback source of
  /// the ObjC runtime when an exe_ctx-less query needs to complete an ObjC
  /// interface from the runtime -- notably GetIndexOfChildMemberWithName, which
  /// the SBFrame::GetValueForVariablePath path calls without any exe_ctx, yet
  /// must still resolve a hidden ivar reconstructed from the runtime (see the
  /// hidden-ivars test). Only a fallback: an explicit exe_ctx always wins.
  lldb::ProcessWP m_last_seen_process_wp;

  // Serializes all mutation of m_context (and the Type nodes it owns) so the
  // DWARF parser can resolve referenced types on worker threads. Recursive so
  // that a locked resolution can nest further locked operations on the same
  // thread. Acquired by constructing a cpp_typesystem::Builder.
  std::recursive_mutex m_mutex;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPESYSTEMCPP_H
