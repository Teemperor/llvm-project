//===-- Builder.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILDER_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILDER_H

#include "Context.h"

#include "lldb/Symbol/CompilerType.h"
#include "lldb/lldb-enumerations.h"

#include <cstdint>
#include <mutex>
#include <optional>

namespace lldb_private {

class TypeSystemCpp;

namespace cpp_typesystem {

/// The mutating, non-thread-safe interface used to populate a TypeSystemCpp
/// (e.g. by the DWARF parser). Constructing a Builder acquires the type
/// system's mutex and holds it for the Builder's lifetime, so every mutation
/// performed through it is serialized against other threads. This is the only
/// way to reach the mutating API: a caller that wants to change the type
/// system must name a Builder, and naming one takes the lock.
class Builder {
public:
  /// Acquire exclusive, serialized access to \p ts's mutable state. The lock
  /// is held until this Builder is destroyed.
  explicit Builder(TypeSystemCpp &ts);

  Builder(const Builder &) = delete;
  Builder &operator=(const Builder &) = delete;

  // Type creation.
  CompilerType GetBuiltinType(llvm::StringRef name,
                              std::optional<uint64_t> byte_size,
                              lldb::Encoding encoding, lldb::Format format);
  /// The `void` builtin type. DWARF encodes `void` as the absence of a
  /// DW_AT_type, so the parser uses this to fill in the underlying type of a
  /// cv-qualified or typedef'd `void` (e.g. the pointee of a `const void *`).
  CompilerType GetVoidType();
  CompilerType CreateRecordType(llvm::StringRef name,
                                std::optional<uint64_t> byte_size,
                                bool is_cpp_class, bool is_union = false,
                                bool is_class_keyword = false);
  /// Create an Objective-C class type (`@interface`). Ivars are added as fields
  /// (via AddField) and the superclass via SetObjCSuperClass during completion.
  CompilerType CreateObjCInterfaceType(llvm::StringRef name,
                                       std::optional<uint64_t> byte_size);
  /// Create an array of \p num_elements elements of \p element_type (or an
  /// array of unknown bound when \p num_elements is std::nullopt).
  CompilerType CreateArrayType(CompilerType element_type,
                               std::optional<uint64_t> num_elements);
  /// Create a pointer to \p pointee_type (an empty CompilerType denotes a
  /// `void *`).
  CompilerType CreatePointerType(CompilerType pointee_type);
  /// Create an Apple "blocks" pointer (`int (^)(int)`) over \p pointee_type
  /// (a FunctionType).
  CompilerType CreateBlockPointerType(CompilerType pointee_type);
  /// Create an lvalue or rvalue reference to \p pointee_type.
  CompilerType CreateReferenceType(CompilerType pointee_type, bool is_rvalue);
  /// Create a pointer-to-member of \p containing_type, pointing at a data
  /// member/member function of type \p pointee_type.
  CompilerType CreateMemberPointerType(CompilerType pointee_type,
                                       CompilerType containing_type);
  /// Create a typedef named \p name aliasing \p underlying_type.
  CompilerType CreateTypedefType(llvm::StringRef name,
                                 CompilerType underlying_type);
  /// Create a const- and/or volatile-qualified version of \p underlying_type.
  CompilerType CreateCVQualifiedType(CompilerType underlying_type,
                                     bool is_const, bool is_volatile);
  /// Create a `__ptrauth`-qualified version of \p underlying_type (a signed
  /// pointer, or a typedef thereof).
  CompilerType CreatePtrAuthType(CompilerType underlying_type, unsigned key,
                                 bool addr_discriminated,
                                 unsigned extra_discriminator);
  /// Create display sugar preserving the source \p spelling (e.g. `::Struct`)
  /// over \p underlying_type. Transparent apart from the display name.
  CompilerType CreateElaboratedType(llvm::StringRef spelling,
                                     CompilerType underlying_type);
  /// Create an enumeration type. Enumerators are added afterwards via
  /// AddEnumerator.
  CompilerType CreateEnumType(llvm::StringRef name,
                              std::optional<uint64_t> byte_size,
                              CompilerType underlying_type, bool is_scoped);
  /// Create a function type. Parameters are added afterwards via AddParameter.
  /// \p use_void_for_empty_params controls how an empty, non-variadic
  /// parameter list is later rendered -- see FunctionType::UseVoidForEmptyParams.
  CompilerType CreateFunctionType(CompilerType return_type, bool is_variadic,
                                  bool use_void_for_empty_params = false);
  /// Create a `_Complex` type over \p element_type.
  CompilerType CreateComplexType(CompilerType element_type);
  /// Append a parameter type to a FunctionType created by CreateFunctionType.
  /// \p name is the parameter's declared name (empty if unnamed), preserved so
  /// a synthesized clang ParmVarDecl can carry it into diagnostics.
  void AddParameter(CompilerType function_type, CompilerType param_type,
                    llvm::StringRef name = {});
  /// Add a member function to \p record. Member functions are C++-only, so this
  /// takes a ClassType.
  void AddMemberFunction(ClassType &record, llvm::StringRef name,
                         CompilerType function_type, llvm::StringRef asm_label,
                         llvm::StringRef mangled_name, bool is_static,
                         bool is_const, bool is_volatile, bool is_virtual,
                         RefQualifier ref_qualifier,
                         MemberFunctionKind kind = MemberFunctionKind::Method);
  /// Add a static data member to \p record. \p mangled_name is the linkage name
  /// used to resolve the member's runtime storage (empty for a constant-only
  /// member); \p const_value is its compile-time constant, if any. Static data
  /// members are C++-only, so this takes a ClassType.
  void AddStaticDataMember(ClassType &record, llvm::StringRef name,
                           Type *type, llvm::StringRef mangled_name,
                           std::optional<uint64_t> const_value);
  /// Intern a name into the Context so it can be used for a type or record
  /// member. All Identifiers must be created this way.
  Identifier GetIdentifier(llvm::StringRef name);

  /// Intern a namespace (see Context::GetNamespace).
  const Namespace *GetNamespace(llvm::StringRef name, const Namespace *parent,
                                bool is_inline);
  /// Record the namespace a type is declared in and its unqualified spelling,
  /// used to build the (inline-namespace-aware) display name.
  void SetDeclContext(CompilerType type, const Namespace *ns);
  void SetUnqualifiedName(CompilerType type, llvm::StringRef name);

  // Structural completion of a record type.
  void SetRecordComplete(RecordType &record);
  /// Mark \p record as a class-template instantiation (see
  /// RecordType::IsTemplateInstantiation). Set even for a specialization over an
  /// empty parameter pack, which has no arguments but still prints `<>`. Only a
  /// C++ class can be a template instantiation, so this takes a ClassType.
  void SetRecordTemplateInstantiation(ClassType &record);
  /// Mark \p record as an anonymous struct/union (an unnamed record embedded as
  /// an unnamed member of its parent; see
  /// RecordType::IsAnonymousStructOrUnion).
  void SetRecordAnonymousStructOrUnion(RecordType &record);
  /// Like the above, but also records \p parent as the enclosing record (see
  /// RecordType::GetAnonymousParent), used to recover a display-name scope
  /// prefix (e.g. `MySock::(anonymous union)`) for a type with no name of its
  /// own to qualify.
  void SetRecordAnonymousStructOrUnion(RecordType &record,
                                       const RecordType &parent);
  /// Record how \p record is passed/returned by value (from DWARF's
  /// DW_AT_calling_convention); see RecordType::ArgPassingKind.
  void SetRecordArgPassingKind(RecordType &record,
                               RecordType::ArgPassingKind kind);
  /// Mark \p record's member functions as parsed (they are filled in lazily,
  /// after completion; see DWARFASTParserCpp::CompleteMemberFunctionsFromDWARF).
  void SetRecordMemberFunctionsParsed(RecordType &record);
  void AddField(RecordType &record, Identifier name, Type *type,
                uint64_t byte_offset, uint32_t bitfield_bit_size = 0,
                uint32_t bitfield_bit_offset = 0);
  void AddBaseClass(ClassType &record, Type *type, uint64_t byte_offset,
                    bool is_virtual = false,
                    std::optional<uint64_t> vbase_offset_offset = std::nullopt);
  /// Mark \p record as a polymorphic C++ class (it -- or a base -- has a
  /// vtable). Recorded eagerly during completion so C++ dynamic-type detection
  /// need not force lazy member-function parsing (see ClassType::IsPolymorphic).
  void SetRecordPolymorphic(ClassType &record);
  /// Set \p record's Objective-C superclass (its single base class).
  void SetObjCSuperClass(ObjCInterfaceType &record, Type *superclass);
  /// Add an Objective-C method to \p record. \p name is the full method name
  /// (`+[Class sel:]` / `-[Class sel:]`); \p function_type's parameters exclude
  /// the implicit self/_cmd; \p asm_label is the FunctionCallLabel the call
  /// resolves through.
  void AddObjCMethod(ObjCInterfaceType &record, llvm::StringRef name,
                     CompilerType function_type, llvm::StringRef asm_label,
                     bool is_class_method, bool is_variadic,
                     bool is_direct = false,
                     bool returns_instancetype = false);
  void AddTemplateArgument(ClassType &record,
                           lldb::TemplateArgumentKind kind, Type *type,
                           uint64_t integral_value, bool is_default);
  /// Add a template-template argument (a template-template parameter such as
  /// the `T1` in `C<float, T1>`), which is not a modeled type and so is kept by
  /// name only.
  void AddTemplateTemplateArgument(ClassType &record, llvm::StringRef name,
                                   bool is_default);
  void AddNestedType(RecordType &record, Identifier name, Type *type);
  void AddEnumerator(EnumType &enum_type, Identifier name, uint64_t value);

private:
  /// Wrap a CompilerType into a TypeRef that pairs the referenced Type with the
  /// Context that owns it (derived from the CompilerType's own type system, so
  /// the reference stays correct even when it points into another Context). An
  /// empty CompilerType (e.g. the `void *` pointee) yields an empty TypeRef.
  static TypeRef ToTypeRef(const CompilerType &type);
  /// Wrap a Type that this Builder's Context owns (e.g. one the DWARF parser
  /// resolved through this type system) into a TypeRef.
  TypeRef ToTypeRef(Type *type) const;

  TypeSystemCpp &m_ts;
  std::lock_guard<std::recursive_mutex> m_lock;
};

} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_BUILDER_H
