//===-- Context.h ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_CONTEXT_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_CONTEXT_H

#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include "BuiltinTypes.h"
#include "LanguageOpts.h"
#include "Namespace.h"
#include "Type.h"
#include "TypeC.h"
#include "TypeObjC.h"

#include "llvm/Support/Casting.h"

#include <map>
#include <tuple>

namespace lldb_private {
namespace clike {

/// Owns all the Type nodes for a TypeSystemClike and hands out stable pointers to
/// them. Types live as long as the Context (and therefore the TypeSystemClike).
class Context {
public:
  /// LanguageOpts is move-only, so the Context takes ownership of \p opts; it
  /// is initialized before the builtins because they are sized from it.
  explicit Context(LanguageOpts opts)
      : m_opts(std::move(opts)), builtin_types(m_opts, identifiers) {
    // The canonical builtins are created by KnownBuiltinTypes rather than
    // through Track, so claim ownership of them here.
    builtin_types.ForEachType(
        [this](Type *type) { type->SetOwningContext(*this); });
  }

  const LanguageOpts &GetLanguageOpts() const { return m_opts; }

  /// The target's pointer width in bytes (from the triple). Used to size
  /// pointer/reference types, which don't store a size of their own.
  uint64_t GetPointerSize() const {
    return m_opts.GetBuiltinSizes().pointer_size;
  }

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

  /// Create a record type. \p is_class_keyword records whether the source
  /// used the `class` keyword (only used to name an unnamed record). \p scope
  /// is where the record is declared (see DeclScope).
  RecordType *CreateRecordType(llvm::StringRef name,
                               std::optional<uint64_t> byte_size,
                               bool is_union = false,
                               bool is_class_keyword = false,
                               DeclScope scope = {});

  /// Create an Objective-C class type (`@interface`). Its ivars are added as
  /// fields and its superclass via SetSuperClass during completion.
  ObjCInterfaceType *CreateObjCInterfaceType(llvm::StringRef name,
                                             std::optional<uint64_t> byte_size,
                                             DeclScope scope = {});

  /// Create an array type of \p num_elements elements of \p element_type.
  /// \p num_elements is std::nullopt for an array of unknown bound. See
  /// ArrayType for \p is_vector and \p die_uid.
  ArrayType *CreateArrayType(TypeRef element_type,
                             std::optional<uint64_t> num_elements,
                             bool is_vector = false,
                             uint64_t die_uid = UINT64_MAX);

  /// Create a pointer type pointing to \p pointee_type. A `void *` points at
  /// the `void` builtin (see TypeRef), not at "nothing". Its byte size is the
  /// target's pointer size.
  PointerType *CreatePointerType(TypeRef pointee_type);

  /// Create an Apple "blocks" pointer (`int (^)(int)`), whose pointee is a
  /// FunctionType. Its byte size is the target's pointer size.
  BlockPointerType *CreateBlockPointerType(TypeRef pointee_type);

  /// Create a typedef named \p name aliasing \p underlying_type, declared in
  /// \p scope.
  TypedefType *CreateTypedefType(llvm::StringRef name, TypeRef underlying_type,
                                 DeclScope scope = {});

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

  /// Create an enumeration type with the given \p enumerators, declared in
  /// \p scope. \p underlying_type is the integer type backing the enum.
  EnumType *CreateEnumType(llvm::StringRef name,
                           std::optional<uint64_t> byte_size,
                           TypeRef underlying_type, bool is_scoped,
                           std::vector<Enumerator> enumerators = {},
                           DeclScope scope = {});

  /// Create a function type with the given return and parameter types.
  /// \p param_names holds the parameters' declared names (it may be shorter
  /// than \p params, leaving the rest unnamed). \p use_void_for_empty_params
  /// controls how an empty, non-variadic parameter list is later rendered --
  /// see FunctionType::UseVoidForEmptyParams.
  FunctionType *CreateFunctionType(TypeRef return_type,
                                   llvm::ArrayRef<TypeRef> params,
                                   bool is_variadic,
                                   bool use_void_for_empty_params = false,
                                   llvm::ArrayRef<Identifier> param_names = {});

  /// Create a `_Complex` type over \p element_type. Its byte size is twice the
  /// element's.
  ComplexType *CreateComplexType(TypeRef element_type);

  /// Structural mutation of already-created record types. These are the gated
  /// entry points for the mutations that happen during lazy completion; the
  /// corresponding Type methods are private and befriend this class.
  /// @{
  void SetComplete(RecordType &record) { record.SetIsComplete(true); }
  void SetByteSize(RecordType &record, uint64_t byte_size) {
    record.SetByteSizeFromDefinition(byte_size);
  }
  void SetExplicitAlignmentInBits(RecordType &record, uint64_t align) {
    record.SetExplicitAlignmentInBits(align);
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
    AssertOwnsType(record);
    record.AddField(name, type, byte_offset, bitfield_bit_size,
                    bitfield_bit_offset);
  }
  void SetObjCSuperClass(ObjCInterfaceType &record, TypeRef superclass) {
    AssertOwnsType(record);
    record.SetSuperClass(superclass);
  }
  void AddObjCMethod(ObjCInterfaceType &record, ObjCMethod method) {
    AssertOwnsType(record);
    record.AddObjCMethod(std::move(method));
  }
  void AddNestedType(RecordType &record, Identifier name, TypeRef type) {
    AssertOwnsType(record);
    record.AddNestedType(name, type);
  }
  /// @}

  /// Intern a namespace. Namespaces are deduplicated by (parent, name, inline),
  /// so a given namespace maps to a single stable Namespace instance owned by
  /// this Context. \p parent is null for a top-level namespace.
  const Namespace *GetNamespace(Identifier name, const Namespace *parent,
                                bool is_inline);

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
    result->SetOwningContext(*this);
    m_types.push_back(std::move(type));
    return result;
  }

  /// Every entry point that creates or mutates a type checks that this Context
  /// is the one that owns it, so a mutation aimed at the wrong Context is
  /// caught where it is introduced. The types a type *references* are not
  /// checked: a reference may name a type another Context owns (see
  /// Type::GetOwningContext, which keeps that recoverable). Compiles away
  /// entirely without assertions.
  void AssertOwnsType(const Type &type) const {
    assert(Owns(&type) &&
           "this Context does not own the type being created or modified");
  }

  /// Whether this Context owns \p type -- i.e. created it, or holds it as one of
  /// its canonical builtins. Cheap, and available in every build: a type records
  /// its owner (Type::GetOwningContext).
  bool Owns(const Type *type) const {
    return type && &type->GetOwningContext() == this;
  }

  std::vector<std::unique_ptr<Type>> m_types;
  /// Interned namespaces, owned for the Context's lifetime, plus a dedup map
  /// keyed by (parent, interned-name pointer, is_inline).
  std::vector<std::unique_ptr<Namespace>> m_namespaces;
  std::map<std::tuple<const Namespace *, const void *, bool>, const Namespace *>
      m_namespace_map;
  /// Uniquing map for pointer types, keyed by (pointee type, is-block), so that
  /// two independently-formed `T *` are the same instance (needed for type
  /// equality) and a plain `T *` stays distinct from a block `T (^)`. See
  /// CreatePointerType / CreateBlockPointerType.
  std::map<std::pair<Type *, bool>, PointerType *> m_pointer_map;
  /// Target/language configuration (triple, builtin sizes, float semantics).
  LanguageOpts m_opts;
  IdentifierMap identifiers;
  /// This contains builtin types of C/C++/Objective-C such as
  /// int, long, etc.
  KnownBuiltinTypes builtin_types;
};

}
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_CONTEXT_H
