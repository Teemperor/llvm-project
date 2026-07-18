//===-- Context.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H

#include <memory>
#include <optional>
#include <vector>

#include "BuiltinTypes.h"
#include "LanguageOpts.h"
#include "Namespace.h"
#include "Type.h"

#include "llvm/Support/Casting.h"

#include <map>
#include <tuple>

namespace lldb_private {
namespace cpp_typesystem {

/// A declaration that TypeSystemCpp hands out as an opaque CompilerDecl. It is a
/// tagged reference to one of the modeled declaration kinds; the tag lets the
/// TypeSystem's Decl* query methods interpret the payload without unsafe
/// pointer type-punning (there is no clang::Decl here).
struct Decl {
  enum class Kind { StaticDataMember, MemberFunction };
  Kind kind;
  const void *payload;
};

/// Owns all the Type nodes for a TypeSystemCpp and hands out stable pointers to
/// them. Types live as long as the Context (and therefore the TypeSystemCpp).
class Context {
public:
  explicit Context(const LanguageOpts &opts)
      : m_opts(opts), builtin_types(opts, identifiers) {}

  const LanguageOpts &GetLanguageOpts() const { return m_opts; }

  /// Returns a builtin type for the given attributes. When the attributes
  /// match one of the enumerated C/C++/Objective-C builtin types, the shared
  /// canonical instance is returned; otherwise a bespoke type is created and
  /// tracked by this Context.
  BuiltinType *GetBuiltinType(llvm::StringRef name,
                              std::optional<uint64_t> byte_size,
                              lldb::Encoding encoding, lldb::Format format);

  /// The canonical builtin type instance for a specific builtin kind (e.g.
  /// BuiltinKind::Int). Used to answer queries for "basic" types by kind.
  BuiltinType *GetBuiltinType(BuiltinKind kind) {
    return builtin_types.Get(kind);
  }

  /// The canonical builtin type whose spelling is \p name (e.g. "unsigned
  /// long"), or nullptr if no builtin is spelled that way. Used to answer type
  /// lookups by name.
  BuiltinType *GetBuiltinTypeByName(llvm::StringRef name) {
    return builtin_types.MatchByName(name);
  }

  /// Create a record type. When \p is_cpp_class is true the record can carry
  /// C++-only information (base classes) and a ClassType is created; otherwise
  /// a plain StructType is used. \p is_class_keyword records whether the source
  /// used the `class` keyword (only used to name an unnamed record).
  RecordType *CreateRecordType(llvm::StringRef name,
                               std::optional<uint64_t> byte_size,
                               bool is_cpp_class, bool is_union = false,
                               bool is_class_keyword = false);

  /// Create an Objective-C class type (`@interface`). Its ivars are added as
  /// fields and its superclass via SetSuperClass during completion.
  ObjCInterfaceType *CreateObjCInterfaceType(llvm::StringRef name,
                                             std::optional<uint64_t> byte_size);

  /// Create an array type of \p num_elements elements of \p element_type.
  /// \p num_elements is std::nullopt for an array of unknown bound.
  ArrayType *CreateArrayType(TypeRef element_type,
                             std::optional<uint64_t> num_elements);

  /// Create a pointer type pointing to \p pointee_type (which may be empty for
  /// `void *`). Its byte size is the target's pointer size. \p is_block marks
  /// an Apple "blocks" pointer (`int (^)(int)`), whose pointee is a
  /// FunctionType.
  PointerType *CreatePointerType(TypeRef pointee_type, bool is_block = false);

  /// Create an lvalue (`T &`) or rvalue (`T &&`) reference to \p pointee_type.
  /// Its byte size is the target's pointer size.
  ReferenceType *CreateReferenceType(TypeRef pointee_type, bool is_rvalue);

  /// Create a pointer-to-member of \p containing_type, pointing at a data
  /// member/member function of type \p pointee_type. Its byte size follows
  /// the Itanium ABI: one pointer for a data member, two for a member
  /// function (the function pointer plus the `this` adjustment).
  MemberPointerType *CreateMemberPointerType(TypeRef pointee_type,
                                             TypeRef containing_type);

  /// Create a typedef named \p name aliasing \p underlying_type.
  TypedefType *CreateTypedefType(llvm::StringRef name, TypeRef underlying_type);

  /// Create a cv-qualified version of \p underlying_type.
  CVQualifiedType *CreateCVQualifiedType(TypeRef underlying_type, bool is_const,
                                         bool is_volatile);

  /// Create a pointer-authentication-qualified version of \p underlying_type
  /// (a `T *__ptrauth(key, addr_disc, extra)` type). \p underlying_type is the
  /// signed pointer (or a typedef thereof).
  PtrAuthType *CreatePtrAuthType(TypeRef underlying_type, unsigned key,
                                 bool addr_discriminated,
                                 unsigned extra_discriminator);

  /// Create display sugar over \p underlying_type that preserves the source
  /// \p spelling (e.g. `::Struct`) for the display name while remaining
  /// transparent for the canonical type name.
  ElaboratedType *CreateElaboratedType(llvm::StringRef spelling,
                                       TypeRef underlying_type);

  /// Create an enumeration type. \p underlying_type is the integer type backing
  /// the enum (may be empty when unknown). Enumerators are added afterwards via
  /// AddEnumerator during completion.
  EnumType *CreateEnumType(llvm::StringRef name,
                           std::optional<uint64_t> byte_size,
                           TypeRef underlying_type, bool is_scoped);

  /// Create a function type with the given return type. Parameters are added
  /// afterwards via AddParameter. \p use_void_for_empty_params controls how an
  /// empty, non-variadic parameter list is later rendered -- see
  /// FunctionType::UseVoidForEmptyParams.
  FunctionType *CreateFunctionType(TypeRef return_type, bool is_variadic,
                                   bool use_void_for_empty_params = false);

  /// Create a `_Complex` type over \p element_type. Its byte size is twice the
  /// element's.
  ComplexType *CreateComplexType(TypeRef element_type);

  /// Structural mutation of already-created record types. These are the gated
  /// entry points for the mutations that happen during lazy completion; the
  /// corresponding Type methods are private and befriend this class.
  /// @{
  void SetComplete(RecordType &record) { record.SetIsComplete(true); }
  void SetTemplateInstantiation(RecordType &record) {
    record.SetIsTemplateInstantiation(true);
  }
  void SetMemberFunctionsParsed(RecordType &record) {
    record.SetMemberFunctionsParsed();
  }
  void SetAnonymousStructOrUnion(RecordType &record) {
    record.SetIsAnonymousStructOrUnion(true);
  }
  void SetAnonymousStructOrUnion(RecordType &record,
                                 const RecordType &parent) {
    record.SetIsAnonymousStructOrUnion(true);
    record.SetAnonymousParent(&parent);
  }
  void SetArgPassingKind(RecordType &record,
                         RecordType::ArgPassingKind kind) {
    record.SetArgPassingKind(kind);
  }
  void AddField(RecordType &record, Identifier name, TypeRef type,
                uint64_t byte_offset, uint32_t bitfield_bit_size = 0,
                uint32_t bitfield_bit_offset = 0) {
    record.AddField(name, type, byte_offset, bitfield_bit_size,
                    bitfield_bit_offset);
  }
  void AddBaseClass(ClassType &record, TypeRef type, uint64_t byte_offset,
                    bool is_virtual = false,
                    std::optional<uint64_t> vbase_offset_offset = std::nullopt) {
    record.AddBaseClass(type, byte_offset, is_virtual, vbase_offset_offset);
  }
  void SetPolymorphic(ClassType &record) { record.SetPolymorphic(); }
  void SetObjCSuperClass(ObjCInterfaceType &record, TypeRef superclass) {
    record.SetSuperClass(superclass);
  }
  void AddObjCMethod(ObjCInterfaceType &record, ObjCMethod method) {
    record.AddObjCMethod(std::move(method));
  }
  void AddTemplateArgument(RecordType &record, TemplateArgument arg) {
    record.AddTemplateArgument(arg);
  }
  void AddNestedType(RecordType &record, Identifier name, TypeRef type) {
    record.AddNestedType(name, type);
  }
  void AddEnumerator(EnumType &enum_type, Identifier name, uint64_t value) {
    enum_type.AddEnumerator(name, value);
  }
  void AddParameter(FunctionType &func, TypeRef type) {
    func.AddParameter(type);
  }
  void AddMemberFunction(RecordType &record, MemberFunction method) {
    record.AddMemberFunction(method);
  }
  void AddStaticDataMember(RecordType &record, StaticDataMember member) {
    record.AddStaticDataMember(member);
  }
  /// @}

  /// Intern a namespace. Namespaces are deduplicated by (parent, name, inline),
  /// so a given namespace maps to a single stable Namespace instance owned by
  /// this Context. \p parent is null for a top-level namespace.
  const Namespace *GetNamespace(Identifier name, const Namespace *parent,
                                bool is_inline);

  /// Intern a declaration reference (a static data member or member function),
  /// returning a stable pointer for use as a CompilerDecl's opaque pointer.
  /// Deduplicated by (kind, payload) so the same member always maps to the same
  /// Decl (needed for CompilerDecl equality/ordering).
  const Decl *GetOrCreateDecl(Decl::Kind kind, const void *payload);

  /// Intern a name into this Context's IdentifierMap. All Identifiers used by
  /// types owned by this Context must be created here so that their backing
  /// storage lives exactly as long as the Context (and its types).
  Identifier GetIdentifier(llvm::StringRef name) {
    return identifiers.get(name);
  }

  /// Invoke \p callback for every RecordType owned by this Context, in creation
  /// order. Used by `target modules dump ast` to synthesize a Clang AST of all
  /// the record types the module has produced.
  template <typename F> void ForEachRecordType(F callback) const {
    for (const std::unique_ptr<Type> &type : m_types)
      if (auto *record = llvm::dyn_cast<RecordType>(type.get()))
        callback(record);
  }

private:
  template <typename T> T *Track(std::unique_ptr<T> type) {
    T *result = type.get();
    m_types.push_back(std::move(type));
    return result;
  }

  std::vector<std::unique_ptr<Type>> m_types;
  /// Interned namespaces, owned for the Context's lifetime, plus a dedup map
  /// keyed by (parent, interned-name pointer, is_inline).
  std::vector<std::unique_ptr<Namespace>> m_namespaces;
  std::map<std::tuple<const Namespace *, const void *, bool>, const Namespace *>
      m_namespace_map;
  /// Uniquing map for pointer types, keyed by (pointee type, is_block), so that
  /// two independently-formed `T *` are the same instance (needed for type
  /// equality). See CreatePointerType.
  std::map<std::pair<Type *, bool>, PointerType *> m_pointer_map;
  /// Interned CompilerDecls (static data members / member functions), owned for
  /// the Context's lifetime and deduplicated by (kind, payload).
  std::vector<std::unique_ptr<Decl>> m_decls;
  std::map<std::pair<Decl::Kind, const void *>, const Decl *> m_decl_map;
  /// Target/language configuration (triple, builtin sizes, float semantics).
  LanguageOpts m_opts;
  IdentifierMap identifiers;
  /// This contains builtin types of C/C++/Objective-C such as
  /// int, long, etc.
  KnownBuiltinTypes builtin_types;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_CONTEXT_H
