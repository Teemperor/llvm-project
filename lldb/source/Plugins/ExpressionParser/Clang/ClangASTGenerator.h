//===-- ClangASTGenerator.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTGENERATOR_H
#define LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTGENERATOR_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/lldb-forward.h"

#include "clang/AST/CharUnits.h"
#include "clang/AST/Type.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/TargetParser/Triple.h"

#include <memory>

namespace clang {
class ASTContext;
class DeclContext;
class DeclarationName;
class NamespaceDecl;
class RecordDecl;
class CXXRecordDecl;
class FieldDecl;
class FunctionDecl;
class TagDecl;
class ObjCInterfaceDecl;
} // namespace clang

namespace lldb_private {

class TypeSystemCpp;

namespace cpp_typesystem {
class Type;
class RecordType;
class ObjCInterfaceType;
class Namespace;
struct ObjCMethod;
} // namespace cpp_typesystem

/// Translates types from the module-level TypeSystemCpp (see the
/// cpp_typesystem::Type hierarchy) into a Clang AST, so that LLDB's Clang-based
/// expression parser can reason about them.
///
/// This is the TypeSystemCpp counterpart of what the ASTImporter does for the
/// Clang module type system: it materializes clang::Decls/QualTypes for the
/// entities referenced by an expression. Unlike the importer, it never copies
/// from another Clang AST; it synthesizes the AST from scratch out of the
/// (Clang-AST-free) cpp_typesystem description, writing directly into a
/// clang::ASTContext. It intentionally does not depend on TypeSystemClang.
///
/// Records are created lazily as forward declarations and are completed (and
/// laid out) on demand, mirroring how debug info is parsed lazily. The
/// generator also keeps the reverse mapping (generated Clang type ->
/// originating cpp_typesystem type) so an expression's result type can be
/// mapped back onto a TypeSystemCpp type without ever translating Clang types
/// back.
class ClangASTGenerator {
  // Reads the reverse map (m_reverse / m_cross_module_complete) and the
  // ASTContext to map generated Clang types back to their cpp_typesystem origin.
  friend class ClangTypeConverter;

public:
  explicit ClangASTGenerator(clang::ASTContext &ast) : m_ast(ast) {}
  /// Provide the target whose module list is searched when a record can only
  /// be completed from a *different* module than the one it was parsed from
  /// (e.g. a type forward-declared in a shared library but fully defined in the
  /// main executable). TypeSystemCpp has no cross-module ASTImporter, so this
  /// generator resolves such records by finding the complete definition among
  /// the target's other modules. Optional: without it, cross-module completion
  /// is simply skipped (the record stays a forward declaration).
  void SetTarget(const lldb::TargetSP &target) { m_target = target; }

  /// Dump a Clang AST of \p records (record CompilerTypes owned by \p ts) to
  /// \p output, applying \p filter (a name substring; empty means no filter).
  /// Builds a standalone, throwaway clang::ASTContext for the module's \p
  /// triple, synthesizes a definition for each record into it, and prints it
  /// with clang's AST dumper. This backs `target modules dump ast` for
  /// TypeSystemCpp (the Clang-AST synthesis has to live in this plugin).
  static void DumpRecords(TypeSystemCpp &ts, const llvm::Triple &triple,
                          llvm::ArrayRef<CompilerType> records,
                          llvm::raw_ostream &output, llvm::StringRef filter,
                          bool show_color);

  /// Compute the Itanium virtual-base-offset-offset for the virtual base \p
  /// vbase of the derived record \p derived, in bytes. This is the (positive)
  /// value that, subtracted from the derived object's vtable pointer, yields the
  /// address of the slot holding the vbase's runtime offset (see
  /// cpp_typesystem::BaseClass::vbase_offset_offset and
  /// TypeSystemCpp::ReadVirtualBaseOffset). It is normally recovered directly
  /// from the DWARF DW_AT_data_member_location expression on the inheritance
  /// DIE, but Darwin's dsymutil strips that expression from the .dSYM. This
  /// recomputes it the way TypeSystemClang does: build a throwaway
  /// clang::ASTContext for \p triple, synthesize + lay out the derived record
  /// into it (which makes clang compute the Itanium vtable layout, including
  /// virtual bases -- see LayoutRecord, which omits virtual bases so clang lays
  /// them out itself), and query clang::ItaniumVTableContext. \p derived and
  /// \p vbase must be record CompilerTypes owned by \p ts, with \p vbase a
  /// (direct or indirect) virtual base of \p derived. Returns std::nullopt when
  /// the layout can't be built or the target ABI isn't Itanium.
  static std::optional<uint64_t>
  ComputeVBaseOffsetOffset(TypeSystemCpp &ts, const llvm::Triple &triple,
                           const CompilerType &derived,
                           const CompilerType &vbase);

  /// Translate \p cpp_type (a CompilerType owned by a TypeSystemCpp) into a
  /// clang::QualType. Returns a null QualType on failure.
  clang::QualType Generate(const CompilerType &cpp_type);

  /// Complete a record previously handed out by Generate(). Called by the
  /// external source when Clang requires the full definition. Returns true if
  /// \p tag_decl was created by this generator (and is now complete).
  bool CompleteRecord(clang::TagDecl *tag_decl);

  /// Complete an Objective-C interface previously handed out by Generate().
  /// Called by the external source (via
  /// ExternalASTSource::CompleteType(ObjCInterfaceDecl*)) when Clang requires
  /// the full definition (its ivars/superclass). Returns true if \p iface_decl
  /// was created by this generator (and is now complete).
  bool CompleteObjCInterface(clang::ObjCInterfaceDecl *iface_decl);

  /// Resolve a type declared inside a record created by this generator (a
  /// nested class/union/enum/typedef) by its unqualified \p name, so a
  /// qualified reference like `Record::Nested` resolves. Returns the generated
  /// clang decl (a TagDecl or TypedefNameDecl) parented to \p record_decl, or
  /// null if the record is unknown or has no such nested type. The nested type
  /// is generated lazily (on first lookup) rather than while completing the
  /// record, so a self-referential nested type (common in libc++ containers)
  /// does not drive completion into an infinite recursion.
  clang::NamedDecl *LookupNestedType(const clang::RecordDecl *record_decl,
                                     llvm::StringRef name);

  /// Provide the debug-info layout for a record created by this generator.
  /// Returns false when the record is unknown (letting Clang lay it out).
  bool LayoutRecord(
      const clang::RecordDecl *record_decl, uint64_t &size, uint64_t &alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &field_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &base_offsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits>
          &vbase_offsets);

  /// Build a clang::FunctionDecl (in the translation unit) for a free function
  /// with the given signature and asm label; the JIT resolves the call through
  /// the label. Returns null on failure.
  clang::FunctionDecl *GenerateFunction(llvm::StringRef name,
                                        const CompilerType &function_cpp_type,
                                        llvm::StringRef asm_label);

  /// As above, but names the function with an explicit clang::DeclarationName
  /// (e.g. a CXXOperatorName for a free `operator==`) so operator syntax /
  /// overload resolution binds to it. Returns null on failure.
  clang::FunctionDecl *GenerateFunction(clang::DeclarationName name,
                                        const CompilerType &function_cpp_type,
                                        llvm::StringRef asm_label);

  /// Build a generic variadic `__unknown_anytype ()` FunctionDecl (in the
  /// translation unit) for a code symbol with no debug info (e.g. a libc
  /// function like `strlen`). The expression must cast the call to a concrete
  /// type; the JIT resolves the callee through the symbol's materialized load
  /// address rather than an asm label. Returns null on failure.
  clang::FunctionDecl *GenerateGenericFunction(llvm::StringRef name);

  /// True while this generator is actively synthesizing clang decls. Adding a
  /// named decl to the (external-visible) translation unit makes clang
  /// reconcile that name against the external source; those reentrant lookups
  /// are internal bookkeeping, not genuine references from the expression, so
  /// callers use this to skip expensive global searches while it is set. See
  /// CppExpressionDeclMap::FindExternalVisibleDecls.
  bool IsGenerating() const { return m_generation_depth != 0; }

  /// Register the clang::NamespaceDecl that the decl map created for the
  /// cpp_typesystem namespace \p cpp_ns. Types whose declaration context is
  /// \p cpp_ns are then generated inside \p clang_ns (rather than the
  /// translation unit) so their qualified names mangle correctly for the JIT.
  void RegisterNamespace(const cpp_typesystem::Namespace *cpp_ns,
                         clang::NamespaceDecl *clang_ns);

  /// The clang DeclContext a type declared in \p cpp_ns should be placed in:
  /// the registered clang::NamespaceDecl if one exists, otherwise the
  /// translation unit (the global namespace, or a namespace not yet mapped).
  clang::DeclContext *
  GetDeclContextForNamespace(const cpp_typesystem::Namespace *cpp_ns);

  /// The clang::NamespaceDecl already registered for \p cpp_ns, or null if none
  /// exists yet. Unlike GetDeclContextForNamespace this never materializes a new
  /// decl, so the decl map can reuse an existing one rather than create a
  /// duplicate NamespaceDecl (which would become a redeclaration whose primary
  /// context lacks the map's external visible storage).
  clang::NamespaceDecl *
  GetRegisteredNamespace(const cpp_typesystem::Namespace *cpp_ns) const;

  /// The cpp_typesystem::Namespace a generator-created clang::NamespaceDecl
  /// stands for, or null if \p clang_ns was not produced here. Lets the decl
  /// map build the namespace's lookup map on demand so member lookups (types,
  /// functions, operators) inside a namespace the generator materialized still
  /// route back to it.
  const cpp_typesystem::Namespace *
  GetNamespaceForDecl(const clang::NamespaceDecl *clang_ns) const;

private:
  /// RAII marker for IsGenerating(): held for the duration of each public
  /// AST-synthesizing entry point (so nested/recursive generation stays
  /// "generating" until the outermost call returns).
  class GenerationGuard {
  public:
    explicit GenerationGuard(ClangASTGenerator &gen) : m_gen(gen) {
      ++m_gen.m_generation_depth;
    }
    ~GenerationGuard() { --m_gen.m_generation_depth; }
    GenerationGuard(const GenerationGuard &) = delete;
    GenerationGuard &operator=(const GenerationGuard &) = delete;

  private:
    ClangASTGenerator &m_gen;
  };

  /// Generate the clang type for \p cpp_type, which is owned by \p ts.
  clang::QualType GenerateType(TypeSystemCpp &ts, cpp_typesystem::Type *cpp_type);

  /// Complete a record's fields/bases from its cpp_typesystem description.
  void PopulateRecord(clang::RecordDecl *record_decl);

  /// Delete \p decl's implicit copy constructor/assignment operator if it
  /// declares a move constructor or move assignment operator (the cheap,
  /// no-overload-resolution-needed rule from C++11 [class.copy]p7,p18 /
  /// [class.copy.assign]p2). Called once a record's members are all in place,
  /// so a *different* record that later embeds this one as a field/base gets
  /// a correct answer from hasSimpleCopyConstructor() instead of tripping the
  /// "not yet been computed by Sema" assert in DeclCXX.h.
  void MarkImplicitCopyOpsDeletedByUserMove(clang::CXXRecordDecl *decl);

  /// Complete an Objective-C interface's ivars/superclass from its
  /// cpp_typesystem description. The ivars are added as clang::ObjCIvarDecls
  /// (not FieldDecls) and the superclass wired up via setSuperClass, so clang
  /// lays it out with the Objective-C runtime ABI rather than as a C++ record
  /// (which would misclassify runtime-offset bitfield ivars and assert in
  /// checkBitfieldClipping during expression codegen).
  void PopulateObjCInterface(clang::ObjCInterfaceDecl *iface_decl);

  /// Add a single Objective-C method (\p method, owned by \p ts) to the
  /// generated \p iface_decl as an ObjCMethodDecl, so the expression parser can
  /// type-check and lower a message send. Mirrors
  /// TypeSystemClang::AddMethodToObjCObjectType.
  void AddObjCMethod(clang::ObjCInterfaceDecl *iface_decl, TypeSystemCpp &ts,
                     const cpp_typesystem::ObjCMethod &method);

  /// If \p rec (owned by \p ts) is an incomplete record whose complete
  /// definition lives in a *different* module of the target, find that complete
  /// record and rebind \p ts / \p rec to it. Returns true if a cross-module
  /// complete definition was found and \p ts / \p rec now refer to it. This is
  /// how TypeSystemCpp completes a type that is only forward-declared in the
  /// module it was parsed from (no ASTImporter is involved). Only called from
  /// the by-value completion path (PopulateRecord), never for a type reached
  /// solely through a pointer/reference, so it preserves lazy completion.
  bool RedirectToCrossModuleDefinition(TypeSystemCpp *&ts,
                                       cpp_typesystem::RecordType *&rec);

  /// Like RedirectToCrossModuleDefinition, but for an Objective-C interface
  /// (\p iface, owned by \p ts): if the ObjC runtime knows about more ivars
  /// than \p iface currently has (e.g. some are only visible in the debug
  /// info of a class extension defined in a *different* image/module -- the
  /// "hidden ivars" scenario -- so no same-module or debug-map DWARF search
  /// finds them), rebind \p ts / \p iface to a scratch-context copy completed
  /// from the runtime. Mirrors TypeSystemCpp::GetRuntimeCompletedObjCType,
  /// which ValueObject::GetCompilerType() applies transparently on the
  /// frame-variable/DIL path (via ObjCLanguageRuntime::GetRuntimeType) but
  /// which this expression-evaluator path must apply explicitly since it
  /// operates on the bare cpp_typesystem::Type, not a ValueObject. Returns
  /// true if \p ts / \p iface were rebound.
  bool RedirectObjCInterfaceToRuntimeDefinition(
      TypeSystemCpp *&ts, cpp_typesystem::ObjCInterfaceType *&iface);

  /// Build a clang::ClassTemplateSpecializationDecl (backed by a synthesized
  /// ClassTemplateDecl) for the class-template instantiation \p rec, so a
  /// template-id such as `TestObj<int>` written in an expression resolves: the
  /// parser looks the bare template name (`TestObj`) up as a ClassTemplateDecl
  /// and applies the arguments. \p rec must be a template instantiation
  /// (RecordType::IsTemplateInstantiation()). \p kind is the record's
  /// clang::TagTypeKind, \p decl_ctx its declaration context. Returns the
  /// specialization decl (a CXXRecordDecl), created as a forward declaration
  /// like every other generated record.
  clang::CXXRecordDecl *BuildClassTemplateSpecializationDecl(
      TypeSystemCpp &ts, cpp_typesystem::RecordType *rec,
      clang::TagTypeKind kind, clang::DeclContext *decl_ctx,
      llvm::StringRef base_name);

  /// Complete any record type embedded by value in \p qt (the type itself, or
  /// an array element) so it can be used as a base/field.
  void EnsureComplete(clang::QualType qt);

  /// Map a cpp_typesystem builtin type to a Clang builtin QualType.
  clang::QualType GenerateBuiltin(cpp_typesystem::Type *cpp_type);

  /// Create ParmVarDecls for \p func from the parameter types of \p function_qt
  /// (a FunctionProtoType).
  void BuildParams(clang::FunctionDecl *func, clang::QualType function_qt);

  /// Shared implementation for the GenerateFunction overloads: build a
  /// clang::FunctionDecl in the translation unit with the given name, signature
  /// and (optional) asm label.
  clang::FunctionDecl *BuildFunction(clang::DeclarationName name,
                                     clang::QualType function_qt,
                                     llvm::StringRef asm_label);

  clang::ASTContext &m_ast;

  /// Target whose modules are searched for a cross-module complete definition
  /// (see SetTarget / RedirectToCrossModuleDefinition). May be null.
  lldb::TargetSP m_target;

  /// Nonzero while inside a public AST-synthesizing entry point. See
  /// IsGenerating() / GenerationGuard.
  unsigned m_generation_depth = 0;

  /// cpp_typesystem::Type -> produced clang QualType (opaque pointer).
  llvm::DenseMap<cpp_typesystem::Type *, void *> m_generated;

  /// Named records generated so far, keyed by fully-qualified name. The same
  /// C/C++ record can be described by distinct cpp_typesystem::RecordType
  /// instances in different modules (e.g. a type forward-declared in the main
  /// executable but fully defined in a shared library). Each module has its own
  /// TypeSystemCpp, so those are different cpp types and m_generated (keyed by
  /// cpp type) would materialize a *separate* clang::RecordDecl for each --
  /// making them distinct, incompatible types to Clang's overload resolution
  /// ("no known conversion from 'foo *' to 'foo *'"). There is no ASTImporter
  /// to unify them, so unify by name here: a record whose fully-qualified name
  /// was already generated reuses that first clang decl (ODR: one C/C++ record
  /// per name in a program). Records with an empty name (unnamed /
  /// expression-local) are never entered here.
  llvm::StringMap<void *> m_records_by_name;

  /// cpp_typesystem::Namespace -> the clang::NamespaceDecl the decl map created
  /// for it, so generated types are placed in the matching namespace.
  llvm::DenseMap<const cpp_typesystem::Namespace *, clang::NamespaceDecl *>
      m_namespaces;

  /// Reverse of m_namespaces for decls this generator created itself, so the
  /// decl map can recover the cpp namespace behind a clang::NamespaceDecl it
  /// finds during a qualified lookup.
  llvm::DenseMap<const clang::NamespaceDecl *, const cpp_typesystem::Namespace *>
      m_namespace_decls;

  /// Reverse map: opaque QualType (quals included) -> originating
  /// cpp_typesystem::Type, so cv-qualified variants stay distinct.
  llvm::DenseMap<void *, cpp_typesystem::Type *> m_reverse;

  /// The TypeSystemCpp that actually owns each cpp_typesystem::Type reachable
  /// through m_reverse (i.e. the `ts` GenerateType was called with for it).
  /// ConvertViaReverseMap must build the returned CompilerType against this
  /// TypeSystem rather than always m_target: a type that merely passed through
  /// the expression (e.g. the plain DeclRefExpr type of a local variable) keeps
  /// pointing at the same cpp_typesystem::Type node its owning module parsed,
  /// and that node's completion state lives in that module's TypeSystemCpp
  /// (SymbolFile, forward-decl map, etc.) -- tagging it with m_target instead
  /// would silently make it uncompletable (m_target, the scratch TypeSystemCpp,
  /// has no SymbolFile of its own). Only a type the parser or this generator
  /// actually synthesized into the scratch TS (ConvertRecord et al., which don't
  /// go through this map) should carry m_target.
  llvm::DenseMap<cpp_typesystem::Type *, TypeSystemCpp *> m_type_owner;

  /// For a record that was only forward-declared in the module it was parsed
  /// from but completed from a *different* module (see
  /// RedirectToCrossModuleDefinition), maps that incomplete cpp record to the
  /// complete definition (as a CompilerType carrying the defining module's
  /// TypeSystemCpp). Used by ClangTypeConverter so an expression whose result
  /// type is such a record is sized from the complete definition rather than
  /// the incomplete forward declaration.
  llvm::DenseMap<cpp_typesystem::Type *, CompilerType> m_cross_module_complete;

  /// Per-record bookkeeping for lazy completion and layout.
  struct RecordInfo {
    TypeSystemCpp *ts = nullptr;
    cpp_typesystem::RecordType *cpp_record = nullptr;
    clang::CXXRecordDecl *clang_decl = nullptr;
    bool completed = false;
    /// The record the layout (size, field/base offsets) was actually read
    /// from. Normally the same as cpp_record, but for a record only
    /// forward-declared in its own module it is the complete definition found
    /// in another module (see RedirectToCrossModuleDefinition). LayoutRecord
    /// must read the size/base offsets from the same record PopulateRecord read
    /// the fields from, or Clang's record layout asserts on a size mismatch.
    cpp_typesystem::RecordType *layout_record = nullptr;
    /// Bit offset of every field decl we added while populating this record,
    /// including the synthetic unnamed bitfields inserted to fill the gaps left
    /// by DWARF's omitted padding bitfields. LayoutRecord reports these to
    /// Clang (a plain parallel walk of the cpp fields wouldn't account for the
    /// synthetic ones).
    llvm::DenseMap<const clang::FieldDecl *, uint64_t> field_bit_offsets;
    /// Nested types (by unqualified name) already resolved and parented into
    /// this record via LookupNestedType, so a repeated lookup reuses the decl.
    llvm::StringMap<clang::NamedDecl *> nested_types;
  };
  // Values are held behind unique_ptr so a RecordInfo& stays valid across the
  // recursive GenerateType/completion calls that insert new entries here (a
  // DenseMap rehash would otherwise invalidate references into it).
  llvm::DenseMap<const clang::RecordDecl *, std::unique_ptr<RecordInfo>>
      m_records;

  /// Per-interface bookkeeping for lazy completion of Objective-C classes,
  /// mirroring m_records but for ObjCInterfaceDecls (which are not RecordDecls
  /// so they don't route through m_records / CompleteRecord).
  struct ObjCInterfaceInfo {
    TypeSystemCpp *ts = nullptr;
    cpp_typesystem::ObjCInterfaceType *cpp_iface = nullptr;
    clang::ObjCInterfaceDecl *clang_decl = nullptr;
    bool completed = false;
  };
  llvm::DenseMap<const clang::ObjCInterfaceDecl *,
                 std::unique_ptr<ObjCInterfaceInfo>>
      m_objc_interfaces;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_EXPRESSIONPARSER_CLANG_CLANGASTGENERATOR_H
