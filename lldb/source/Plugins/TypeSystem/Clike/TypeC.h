//===-- TypeC.h -------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The type kinds C already has (and C++/Objective-C inherit unchanged): plain
// records, arrays, pointers, enums, functions, `_Complex`, and the sugar
// wrappers. See Type.h for the language-neutral core and TypeCpp.h /
// TypeObjC.h for the kinds specific to those languages.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_C_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_C_H

#include <cstdint>
#include <optional>
#include <vector>

#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "Identifier.h"
#include "Type.h"

namespace lldb_private {
namespace clike_typesystem {

/// A C struct/union type. Records parsed from a C++ translation unit are
/// backed by ClassType instead, since only those can have base classes.
class StructType : public llvm::RTTIExtends<StructType, RecordType> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassStruct;
  }
};

/// A C array type: a fixed number of contiguous elements of the same type.
class ArrayType : public llvm::RTTIExtends<ArrayType, Type> {
public:
  static char ID;

  Type *GetElementType() const { return m_element_type.Get(); }
  void SetElementType(TypeRef type) { m_element_type = type; }

  /// Number of elements, or std::nullopt for an array of unknown bound.
  std::optional<uint64_t> GetNumElements() const {
    return m_num_elements == kNoNumElements
               ? std::nullopt
               : std::optional<uint64_t>(m_num_elements);
  }
  void SetNumElements(std::optional<uint64_t> num_elements) {
    m_num_elements = num_elements.value_or(kNoNumElements);
  }

  /// The UID of the DWARF DIE that produced this array, or UINT64_MAX (i.e.
  /// LLDB_INVALID_UID). Lets the symbol file re-resolve a runtime
  /// (variable-length) bound for an otherwise-unbounded array. Stored as a
  /// plain uint64_t to avoid pulling lldb-forward.h (and its `lldb_private::Type`
  /// forward declaration, which would make `Type` ambiguous) into this header.
  uint64_t GetDIEUID() const { return m_die_uid; }
  void SetDIEUID(uint64_t uid) { m_die_uid = uid; }

  /// Whether this array is really a GCC/Clang vector type (DW_AT_GNU_vector,
  /// e.g. `float __attribute__((ext_vector_type(4)))`). Vectors are laid out
  /// like arrays but LLDB treats them as vector types for formatting.
  bool IsVector() const { return m_is_vector; }
  void SetIsVector(bool is_vector) { m_is_vector = is_vector; }

  // An array is an aggregate whose children are its elements.
  bool IsAggregate() const override { return true; }
  // The array's storage is the element size times the element count (when both
  // are known). Computed on demand rather than stored: the element size may
  // only become known after the array is created (a target-dependent builtin
  // size or a lazily completed record).
  std::optional<uint64_t> GetByteSize() const override {
    std::optional<uint64_t> num_elements = GetNumElements();
    if (!num_elements)
      return std::nullopt;
    const Type *element = m_element_type.Get();
    if (!element)
      return std::nullopt;
    if (std::optional<uint64_t> elem_size = element->GetByteSize())
      return *elem_size * *num_elements;
    return std::nullopt;
  }
  lldb::TypeClass GetTypeClass() const override {
    return m_is_vector ? lldb::eTypeClassVector : lldb::eTypeClassArray;
  }
  uint32_t GetTypeInfo() const override {
    if (m_is_vector)
      return lldb::eTypeHasChildren | lldb::eTypeIsVector;
    return lldb::eTypeHasChildren | lldb::eTypeIsArray;
  }

private:
  // An array can't really have 2^64-1 elements, so steal that value as the
  // "unbounded" sentinel instead of paying for std::optional's separate bool.
  static constexpr uint64_t kNoNumElements = UINT64_MAX;
  TypeRef m_element_type;
  uint64_t m_num_elements = kNoNumElements;
  uint64_t m_die_uid = UINT64_MAX;
  bool m_is_vector = false;
};

/// A simple pointer type.
class PointerType : public llvm::RTTIExtends<PointerType, Type> {
public:
  static char ID;

  /// The type this pointer points to. E.g., for `int *` this is `int`.
  /// May be null for `void *`.
  Type *GetPointeeType() const { return m_pointee_type.Get(); }
  void SetPointeeType(TypeRef type) { m_pointee_type = type; }

  /// True if this is a function-pointer type (`void (*)(int)`), matching
  /// clang's isFunctionPointerType(). A block pointer (`int (^)(int)`) is not
  /// one despite also pointing at a function type, and neither is a *reference*
  /// to a function (`void (&)(int)`) -- which is why this lives on PointerType
  /// rather than being a free function over any pointee. The distinction
  /// matters for formatting: the C++ "function pointer summary" only applies to
  /// pointers, so a function reference must not pick it up.
  ///
  /// Out-of-line: BlockPointerType and FunctionType are declared below.
  bool IsFunctionPointer() const;

  // A pointer's size is the target's pointer width, so it isn't stored on every
  // pointer: it is recovered from the owning Context (which knows the target
  // triple) reached through the pointee reference. Defined out-of-line because
  // it needs Context's definition. See PointerType::GetByteSize in TypeC.cpp
  // and Context::CreatePointerType (which guarantees the pointee reference
  // always carries a Context, even for `void *`).
  std::optional<uint64_t> GetByteSize() const override;

  // A pointer is pointer-aligned, which is its own size -- not the
  // largest-power-of-two-dividing-the-size heuristic Type applies.
  std::optional<uint64_t> GetAlignmentInBits() const override {
    if (std::optional<uint64_t> size = GetByteSize())
      return *size * 8;
    return std::nullopt;
  }

  lldb::Encoding GetEncoding() const override { return lldb::eEncodingUint; }
  lldb::Format GetFormat() const override { return lldb::eFormatHex; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassPointer;
  }
  // Out-of-line because recognizing a pointer to an Objective-C object needs
  // ObjCInterfaceType, which this (C-only) header must not depend on. See
  // TypeC.cpp.
  uint32_t GetTypeInfo() const override;
  Type *GetTransparentChildPointee() override;
  Type *GetNamedMemberPointee() override;

private:
  TypeRef m_pointee_type;
};

/// An Apple "blocks" pointer (`int (^)(int)`): its pointee is a FunctionType,
/// but the runtime representation and calling convention are those of a block,
/// not a plain function pointer. This is how DWARF's `DW_AT_APPLE_block`
/// closures are represented. A block pointer is a pointer in every layout/value
/// respect, so it derives from PointerType and reuses all of it; it exists as a
/// distinct kind only so that the few places that must treat it specially (the
/// `(^)` vs `(*)` display spelling, mapping to a clang BlockPointerType,
/// IsBlockPointerType / IsFunctionPointerType) can key off the type via
/// llvm::isa<BlockPointerType> instead of a per-pointer flag.
class BlockPointerType
    : public llvm::RTTIExtends<BlockPointerType, PointerType> {
public:
  static char ID;
};

/// A typedef/using alias. It carries its own (alias) name but otherwise behaves
/// like the type it aliases.
class TypedefType : public llvm::RTTIExtends<TypedefType, NamedType<SugarType>> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassTypedef;
  }
  uint32_t GetTypeInfo() const override {
    return SugarType::GetTypeInfo() | lldb::eTypeIsTypedef;
  }
};

/// A `const`- and/or `volatile`-qualified type. Transparent like all sugar; it
/// only records which qualifiers apply so they can be reported and rendered in
/// the type name.
class CVQualifiedType : public llvm::RTTIExtends<CVQualifiedType, SugarType> {
public:
  static char ID;

  bool IsConst() const { return m_is_const; }
  void SetIsConst(bool is_const) { m_is_const = is_const; }
  bool IsVolatile() const { return m_is_volatile; }
  void SetIsVolatile(bool is_volatile) { m_is_volatile = is_volatile; }

  /// The CVR qualifier mask applying to \p t, using clang's bit layout
  /// (Const = 0x1, Restrict = 0x2, Volatile = 0x4) so the result can be handed
  /// straight to clang::Qualifiers::fromCVRMask. DWARF nests one qualifier per
  /// DIE, so `const volatile T` is modeled as two stacked CVQualifiedType
  /// nodes; this walks the whole cv-sugar chain and ORs the flags together so
  /// both are reported (as a single clang `const volatile T` would).
  static unsigned GetCVRMask(const Type *t);

private:
  bool m_is_const = false;
  bool m_is_volatile = false;
};

/// A pointer carrying an ARMv8.3 pointer-authentication qualifier
/// (`T *__ptrauth(key, address_discriminated, extra_discriminator)`). It is
/// transparent sugar over the signed type (a pointer or a typedef thereof) for
/// layout/value purposes, but carries the signing schema so LLDB can strip the
/// authentication bits off the raw pointer value and print `__ptrauth(...)`.
class PtrAuthType : public llvm::RTTIExtends<PtrAuthType, SugarType> {
public:
  static char ID;

  /// The pointer-authentication key (a small integer, e.g. 2 for the data key).
  unsigned GetKey() const { return m_key; }
  void SetKey(unsigned key) { m_key = key; }

  /// True if the pointer is signed with address diversity (the storage address
  /// participates in the discriminator).
  bool IsAddressDiscriminated() const { return m_addr_discriminated; }
  void SetAddressDiscriminated(bool v) { m_addr_discriminated = v; }

  /// The extra (constant) discriminator blended into the signature.
  unsigned GetExtraDiscriminator() const { return m_extra_discriminator; }
  void SetExtraDiscriminator(unsigned v) { m_extra_discriminator = v; }

private:
  unsigned m_key = 0;
  bool m_addr_discriminated = false;
  unsigned m_extra_discriminator = 0;
};

/// Pure display sugar preserving how a type was spelled in the source (e.g.
/// `::Struct`, `$V< ::Struct>`), like clang's elaborated-type / template-type
/// sugar. It is fully transparent: the *display* name uses the stored spelling
/// (so a user's `::Struct` shows as `::Struct`), but the canonical type name
/// desugars past it (so formatters keyed on `Struct` still match), mirroring
/// TypeSystemClang's RemoveWrappingTypes.
class ElaboratedType : public llvm::RTTIExtends<ElaboratedType, SugarType> {
public:
  static char ID;

  /// The source spelling to use for the display name (e.g. `::Struct`).
  Identifier GetSpelling() const { return m_spelling; }
  void SetSpelling(Identifier spelling) { m_spelling = spelling; }

  /// Peel any elaborated display sugar off \p t, stopping at the first type
  /// that carries real meaning. Mirrors TypeSystemClang's
  /// RemoveWrappingTypes({Typedef}): unlike a full Desugar() this keeps a
  /// typedef, since a typedef named through a qualifier (`GlobalTypedef::V`)
  /// is still a typedef and the alias name is meaningful.
  static Type *Strip(Type *t) {
    while (auto *el = llvm::dyn_cast_or_null<ElaboratedType>(t))
      t = el->GetUnderlyingType();
    return t;
  }

private:
  Identifier m_spelling;
};

/// A single (name, value) constant of an enumeration.
struct Enumerator {
  Identifier name;
  /// The constant's value, stored as raw bits; interpret as signed when the
  /// enum's underlying type is signed.
  uint64_t value = 0;
};

/// A C/C++ enumeration type. It has an underlying integer type and a set of
/// named constants; scoped enums (`enum class`) are distinguished so their
/// enumerators are not treated as being in the enclosing scope.
class EnumType : public llvm::RTTIExtends<EnumType, NamedType<ByteSizedType<Type>>> {
public:
  static char ID;

  Type *GetUnderlyingType() const { return m_underlying_type.Get(); }
  void SetUnderlyingType(TypeRef type) { m_underlying_type = type; }

  bool IsScoped() const { return m_is_scoped; }
  void SetIsScoped(bool is_scoped) { m_is_scoped = is_scoped; }

  /// True when the underlying integer type is signed.
  bool IsSigned() const {
    return !m_underlying_type ||
           m_underlying_type.Get()->GetEncoding() == lldb::eEncodingSint;
  }

  const std::vector<Enumerator> &GetEnumerators() const {
    return m_enumerators;
  }

  lldb::Encoding GetEncoding() const override {
    return m_underlying_type ? m_underlying_type.Get()->GetEncoding()
                             : lldb::eEncodingSint;
  }
  lldb::Format GetFormat() const override { return lldb::eFormatEnum; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassEnumeration;
  }
  uint32_t GetTypeInfo() const override {
    // Note: TypeSystemClang deliberately does NOT set eTypeIsScalar (or
    // eTypeIsInteger/eTypeIsSigned) here, even though an enum is a scalar in
    // the C++ sense. DIL's cast-verification logic (and other callers) check
    // IsScalarType() before IsEnumerationType(), so setting eTypeIsScalar
    // would misroute enums into the scalar-cast branch instead of the
    // enumeration-cast branch. Match TypeSystemClang's flags exactly.
    return lldb::eTypeIsEnumeration | lldb::eTypeHasValue;
  }

private:
  // Gated like RecordType's mutators: only Context (through a Builder) may add
  // enumerators.
  friend class Context;
  void AddEnumerator(Identifier name, uint64_t value) {
    m_enumerators.push_back(Enumerator{name, value});
  }

  TypeRef m_underlying_type;
  bool m_is_scoped = false;
  std::vector<Enumerator> m_enumerators;
};

/// A C99 `_Complex` type: two contiguous components (real, imaginary) of an
/// element type. Both `_Complex float/double/long double` (DW_ATE_complex_float)
/// and the Clang extension `_Complex int` (a vendor encoding) are modeled here.
class ComplexType : public llvm::RTTIExtends<ComplexType, Type> {
public:
  static char ID;

  Type *GetElementType() const { return m_element_type.Get(); }
  void SetElementType(TypeRef type) { m_element_type = type; }

  /// Complex-float when the element is a floating-point type, else
  /// complex-integer.
  bool IsFloat() const {
    return m_element_type &&
           m_element_type.Get()->GetEncoding() == lldb::eEncodingIEEE754;
  }

  // A complex value is two contiguous components of the element type. Computed
  // on demand rather than stored (the element size may only be known later).
  std::optional<uint64_t> GetByteSize() const override {
    const Type *element = m_element_type.Get();
    if (!element)
      return std::nullopt;
    if (std::optional<uint64_t> element_size = element->GetByteSize())
      return *element_size * 2;
    return std::nullopt;
  }

  // No lldb::Encoding value describes a complex type.
  lldb::Encoding GetEncoding() const override { return lldb::eEncodingInvalid; }
  lldb::Format GetFormat() const override {
    return IsFloat() ? lldb::eFormatComplexFloat : lldb::eFormatComplexInteger;
  }
  lldb::TypeClass GetTypeClass() const override {
    return IsFloat() ? lldb::eTypeClassComplexFloat
                     : lldb::eTypeClassComplexInteger;
  }
  uint32_t GetTypeInfo() const override {
    uint32_t info = lldb::eTypeIsBuiltIn | lldb::eTypeHasValue |
                    lldb::eTypeIsComplex | lldb::eTypeIsScalar;
    info |= IsFloat() ? lldb::eTypeIsFloat : lldb::eTypeIsInteger;
    return info;
  }

private:
  TypeRef m_element_type;
};

/// A function type: a return type plus parameter types. For a member function
/// the parameters exclude the implicit object (`this`) parameter. Used to build
/// clang::FunctionDecl/CXXMethodDecl signatures for calling functions from
/// expressions.
class FunctionType : public llvm::RTTIExtends<FunctionType, Type> {
public:
  static char ID;

  Type *GetReturnType() const { return m_return_type.Get(); }
  void SetReturnType(TypeRef type) { m_return_type = type; }

  uint32_t GetNumParameters() const { return m_params.size(); }
  Type *GetParameterAtIndex(uint32_t idx) const {
    if (idx < m_params.size())
      return m_params[idx].Get();
    return nullptr;
  }

  /// The declared name of the parameter at \p idx (its DW_AT_name), or an empty
  /// StringRef if the parameter was unnamed. Used so a synthesized clang
  /// ParmVarDecl can carry the original parameter name (e.g. so a diagnostic
  /// reads "requires single argument 'x'" rather than "requires 1 argument").
  llvm::StringRef GetParameterNameAtIndex(uint32_t idx) const {
    if (idx < m_param_names.size())
      return m_param_names[idx].GetName();
    return {};
  }

  bool IsVariadic() const { return m_is_variadic; }
  void SetIsVariadic(bool is_variadic) { m_is_variadic = is_variadic; }

  /// Whether an empty, non-variadic parameter list should be spelled `(void)`
  /// instead of `()`. This mirrors clang's `PrintingPolicy::UseVoidForZeroParams`,
  /// which is true for a prototyped C function (`int foo(void);`) and false for
  /// C++ (where `int foo();` already means an empty prototype). TypeSystemClike
  /// has a single Context shared across C and C++ compile units (unlike
  /// TypeSystemClang, which gets one ASTContext/LangOptions per language), so
  /// this can't be a global printing-policy setting -- it has to be decided per
  /// DW_TAG_subroutine_type/subprogram at parse time from DW_AT_prototyped plus
  /// the owning compile unit's language and stored here.
  bool UseVoidForEmptyParams() const { return m_use_void_for_empty_params; }
  void SetUseVoidForEmptyParams(bool value) {
    m_use_void_for_empty_params = value;
  }

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassFunction;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeIsFuncPrototype | lldb::eTypeHasValue;
  }

private:
  // Gated like the other mutators: only Context (through a Builder) may add
  // parameters.
  friend class Context;
  void AddParameter(TypeRef type, Identifier name = Identifier()) {
    m_params.push_back(type);
    m_param_names.push_back(name);
  }

  TypeRef m_return_type;
  std::vector<TypeRef> m_params;
  // Parallel to m_params: the declared name of each parameter (empty Identifier
  // for an unnamed parameter). Kept separate so the common size/layout queries
  // that only need parameter *types* don't touch it.
  std::vector<Identifier> m_param_names;
  bool m_is_variadic = false;
  bool m_use_void_for_empty_params = false;
};

} // namespace clike_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_C_H
