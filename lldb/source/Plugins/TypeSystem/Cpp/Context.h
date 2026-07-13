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

#include <map>
#include <tuple>

namespace lldb_private {
namespace cpp_typesystem {

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

  /// Create a record type. When \p is_cpp_class is true the record can carry
  /// C++-only information (base classes) and a ClassType is created; otherwise
  /// a plain StructType is used.
  RecordType *CreateRecordType(llvm::StringRef name,
                               std::optional<uint64_t> byte_size,
                               bool is_cpp_class, bool is_union = false);

  /// Create an array type of \p num_elements elements of \p element_type.
  /// \p num_elements is std::nullopt for an array of unknown bound.
  ArrayType *CreateArrayType(TypeRef element_type,
                             std::optional<uint64_t> num_elements);

  /// Create a pointer type pointing to \p pointee_type (which may be empty for
  /// `void *`). Its byte size is the target's pointer size.
  PointerType *CreatePointerType(TypeRef pointee_type);

  /// Create an lvalue (`T &`) or rvalue (`T &&`) reference to \p pointee_type.
  /// Its byte size is the target's pointer size.
  ReferenceType *CreateReferenceType(TypeRef pointee_type, bool is_rvalue);

  /// Create a typedef named \p name aliasing \p underlying_type.
  TypedefType *CreateTypedefType(llvm::StringRef name, TypeRef underlying_type);

  /// Create a cv-qualified version of \p underlying_type.
  CVQualifiedType *CreateCVQualifiedType(TypeRef underlying_type, bool is_const,
                                         bool is_volatile);

  /// Create an enumeration type. \p underlying_type is the integer type backing
  /// the enum (may be empty when unknown). Enumerators are added afterwards via
  /// AddEnumerator during completion.
  EnumType *CreateEnumType(llvm::StringRef name,
                           std::optional<uint64_t> byte_size,
                           TypeRef underlying_type, bool is_scoped);

  /// Create a function type with the given return type. Parameters are added
  /// afterwards via AddParameter.
  FunctionType *CreateFunctionType(TypeRef return_type, bool is_variadic);

  /// Structural mutation of already-created record types. These are the gated
  /// entry points for the mutations that happen during lazy completion; the
  /// corresponding Type methods are private and befriend this class.
  /// @{
  void SetComplete(RecordType &record) { record.SetIsComplete(true); }
  void SetMemberFunctionsParsed(RecordType &record) {
    record.SetMemberFunctionsParsed();
  }
  void AddField(RecordType &record, Identifier name, TypeRef type,
                uint64_t byte_offset, uint32_t bitfield_bit_size = 0,
                uint32_t bitfield_bit_offset = 0) {
    record.AddField(name, type, byte_offset, bitfield_bit_size,
                    bitfield_bit_offset);
  }
  void AddBaseClass(ClassType &record, TypeRef type, uint64_t byte_offset) {
    record.AddBaseClass(type, byte_offset);
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
