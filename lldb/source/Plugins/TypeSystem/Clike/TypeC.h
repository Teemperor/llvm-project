//===-- TypeC.h -----------------------------------------------------------===//
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

#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "ByteSizedType.h"
#include "Identifier.h"
#include "NamedType.h"
#include "RecordType.h"
#include "SugarType.h"
#include "Type.h"

namespace lldb_private {
namespace clike {

/// A C struct/union type. Records parsed from a C++ translation unit are
/// backed by ClassType instead, since only those can have base classes.
class StructType : public llvm::RTTIExtends<StructType, RecordType> {
public:
  using RTTIExtends::RTTIExtends;

  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassStruct;
  }
};

/// A C array type: a fixed number of contiguous elements of the same type.
class ArrayType : public llvm::RTTIExtends<ArrayType, Type> {
public:
  static char ID;

  /// \p num_elements is std::nullopt for an array of unknown bound.
  /// \p is_vector marks a GCC/Clang vector type and \p die_uid is the UID of
  /// the DWARF DIE that produced the array (see IsVector and GetDIEUID).
  ArrayType(TypeRef element_type, std::optional<uint64_t> num_elements,
            bool is_vector = false, uint64_t die_uid = UINT64_MAX)
      : m_element_type(element_type),
        m_num_elements(num_elements.value_or(kNoNumElements)),
        m_die_uid(die_uid), m_is_vector(is_vector) {}

  Type *GetElementType() const { return &m_element_type.Get(); }
  Type *GetArrayElementType() const override { return GetElementType(); }

  /// Number of elements, or std::nullopt for an array of unknown bound.
  std::optional<uint64_t> GetNumElements() const override {
    return m_num_elements == kNoNumElements
               ? std::nullopt
               : std::optional<uint64_t>(m_num_elements);
  }

  /// The UID of the DWARF DIE that produced this array, or UINT64_MAX (i.e.
  /// LLDB_INVALID_UID). Lets the symbol file re-resolve a runtime
  /// (variable-length) bound for an otherwise-unbounded array. Stored as a
  /// plain uint64_t to avoid pulling lldb-forward.h (and its `lldb_private::Type`
  /// forward declaration, which would make `Type` ambiguous) into this header.
  uint64_t GetDIEUID() const override { return m_die_uid; }

  /// Whether this array is really a GCC/Clang vector type (DW_AT_GNU_vector,
  /// e.g. `float __attribute__((ext_vector_type(4)))`). Vectors are laid out
  /// like arrays but LLDB treats them as vector types for formatting.
  bool IsVector() const override { return m_is_vector; }

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
    if (std::optional<uint64_t> elem_size = m_element_type.Get().GetByteSize())
      return *elem_size * *num_elements;
    return std::nullopt;
  }
  /// The alignment of the elements. Vectors are aligned to their size rounded
  /// up to a power of two instead, but at most to the target's maximum vector
  /// alignment.
  std::optional<uint64_t> GetAlignmentInBits() const override;
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
  uint64_t m_num_elements;
  uint64_t m_die_uid;
  bool m_is_vector;
};

/// A simple pointer type.
class PointerType : public llvm::RTTIExtends<PointerType, Type> {
public:
  static char ID;

  explicit PointerType(TypeRef pointee_type) : m_pointee_type(pointee_type) {}

  /// The type this pointer points to. E.g., for `int *` this is `int`. Never
  /// null: a `void *` points at the `void` builtin (see TypeRef), so use
  /// BuiltinType::IsVoid to recognize one rather than testing for absence.
  Type *GetPointeeType() const override { return &m_pointee_type.Get(); }

  bool IsPointer() const override { return true; }

  /// True if this is a function-pointer type (`void (*)(int)`), matching
  /// clang's isFunctionPointerType(). A block pointer (`int (^)(int)`) is not
  /// one despite also pointing at a function type, and neither is a *reference*
  /// to a function (`void (&)(int)`) -- which is why this lives on PointerType
  /// rather than being a free function over any pointee. The distinction
  /// matters for formatting: the C++ "function pointer summary" only applies to
  /// pointers, so a function reference must not pick it up.
  bool IsFunctionPointer() const override {
    return !IsBlockPointer() && m_pointee_type.Get().Desugar()->IsFunction();
  }

  // A pointer's size is the target's pointer width, which is a property of the
  // target rather than of the individual pointer, so it isn't stored per
  // pointer: it is recovered from the Context that owns this type (which knows
  // the target triple). Defined out-of-line because it needs Context's
  // definition. See PointerType::GetByteSize in TypeC.cpp.
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

  // A pointer to an Objective-C object is an Objective-C construct, a pointer
  // to a record a C++ one (so class data formatters still fire), and a pointer
  // to anything else (e.g. `int *`, `enum E *`) a C construct. The latter
  // matters for a null pointer: CPlusPlusLanguage::IsNilReference would print
  // a null C++ pointer as "NULL" instead of its address.
  lldb::LanguageType GetMinimumLanguage() const override;

  Type *
  GetDynamicTypeCandidate(bool check_cplusplus, bool check_objc,
                          llvm::function_ref<void(Type *)> complete) override;

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
  using RTTIExtends::RTTIExtends;

  static char ID;

  bool IsBlockPointer() const override { return true; }
};

/// A typedef/using alias. It carries its own (alias) name but otherwise behaves
/// like the type it aliases.
class TypedefType : public llvm::RTTIExtends<TypedefType, NamedType<SugarType>> {
public:
  using RTTIExtends::RTTIExtends;

  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassTypedef;
  }
  uint32_t GetTypeInfo() const override {
    return SugarType::GetTypeInfo() | lldb::eTypeIsTypedef;
  }
  Type *GetTypedefedType() const override { return GetUnderlyingType(); }
};

/// A `const`- and/or `volatile`-qualified type. Transparent like all sugar; it
/// only records which qualifiers apply so they can be reported and rendered in
/// the type name.
class CVQualifiedType : public llvm::RTTIExtends<CVQualifiedType, SugarType> {
public:
  static char ID;

  CVQualifiedType(TypeRef underlying_type, bool is_const, bool is_volatile)
      : RTTIExtends(underlying_type), m_is_const(is_const),
        m_is_volatile(is_volatile) {}

  bool IsConst() const override { return m_is_const; }
  bool IsVolatile() const { return m_is_volatile; }

  Type *StripCVQualifiers() override {
    return GetUnderlyingType()->StripCVQualifiers();
  }
  // DWARF nests one qualifier per DIE, so `const volatile T` is two stacked
  // CVQualifiedType nodes: OR in whatever the rest of the chain reports.
  unsigned GetCVRQualifiers() const override {
    return (m_is_const ? 0x1 : 0) | (m_is_volatile ? 0x4 : 0) |
           GetUnderlyingType()->GetCVRQualifiers();
  }

private:
  bool m_is_const;
  bool m_is_volatile;
};

/// A pointer carrying an ARMv8.3 pointer-authentication qualifier
/// (`T *__ptrauth(key, address_discriminated, extra_discriminator)`). It is
/// transparent sugar over the signed type (a pointer or a typedef thereof) for
/// layout/value purposes, but carries the signing schema so LLDB can strip the
/// authentication bits off the raw pointer value and print `__ptrauth(...)`.
class PtrAuthType : public llvm::RTTIExtends<PtrAuthType, SugarType> {
public:
  static char ID;

  PtrAuthType(TypeRef underlying_type, PtrAuthQualifier qualifier)
      : RTTIExtends(underlying_type), m_qualifier(qualifier) {}

  /// See PtrAuthQualifier.
  unsigned GetKey() const { return m_qualifier.GetKey(); }
  bool IsAddressDiscriminated() const {
    return m_qualifier.IsAddressDiscriminated();
  }
  unsigned GetExtraDiscriminator() const {
    return m_qualifier.GetExtraDiscriminator();
  }

  std::optional<PtrAuthQualifier> GetPtrAuthQualifier() const override {
    return m_qualifier;
  }

private:
  PtrAuthQualifier m_qualifier;
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

  ElaboratedType(TypeRef underlying_type, Identifier spelling)
      : RTTIExtends(underlying_type), m_spelling(spelling) {}

  /// The source spelling to use for the display name (e.g. `::Struct`).
  Identifier GetSpelling() const { return m_spelling; }

  Type *StripElaborated() override {
    return GetUnderlyingType()->StripElaborated();
  }

private:
  Identifier m_spelling;
};

/// A C/C++ enumeration type. It has an underlying integer type and a set of
/// named constants; scoped enums (`enum class`) are distinguished so their
/// enumerators are not treated as being in the enclosing scope.
class EnumType : public llvm::RTTIExtends<EnumType, NamedType<ByteSizedType<Type>>> {
public:
  static char ID;

  EnumType(Identifier name, DeclScope scope, std::optional<uint64_t> byte_size,
           TypeRef underlying_type, bool is_scoped,
           std::vector<Enumerator> enumerators)
      : RTTIExtends(name, scope, byte_size), m_underlying_type(underlying_type),
        m_is_scoped(is_scoped), m_enumerators(std::move(enumerators)) {}

  /// The integer type backing this enum. Never null: debug info that records
  /// no base type gets the `int` C would have chosen (see
  /// Builder::CreateEnumType).
  Type *GetUnderlyingType() const { return &m_underlying_type.Get(); }
  Type *GetEnumerationIntegerType() const override {
    return GetUnderlyingType();
  }

  bool IsScoped() const { return m_is_scoped; }
  bool IsScopedEnumeration() const override { return IsScoped(); }

  /// True when the underlying integer type is signed.
  bool IsSigned() const {
    return m_underlying_type.Get().GetEncoding() == lldb::eEncodingSint;
  }

  llvm::ArrayRef<Enumerator> GetEnumerators() const override {
    return m_enumerators;
  }

  bool IsInteger() const override {
    lldb::Encoding encoding = GetEncoding();
    return encoding == lldb::eEncodingSint || encoding == lldb::eEncodingUint;
  }
  // Unscoped enumerations promote like the integer types; scoped ones don't
  // convert implicitly at all.
  bool IsPromotableInteger() const override { return !IsScoped(); }

  std::optional<uint64_t> GetAlignmentInBits() const override {
    return m_underlying_type.Get().GetAlignmentInBits();
  }
  lldb::Encoding GetEncoding() const override {
    return m_underlying_type.Get().GetEncoding();
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
  TypeRef m_underlying_type;
  bool m_is_scoped;
  std::vector<Enumerator> m_enumerators;
};

/// A C99 `_Complex` type: two contiguous components (real, imaginary) of an
/// element type. Both `_Complex float/double/long double` (DW_ATE_complex_float)
/// and the Clang extension `_Complex int` (a vendor encoding) are modeled here.
class ComplexType : public llvm::RTTIExtends<ComplexType, Type> {
public:
  static char ID;

  explicit ComplexType(TypeRef element_type) : m_element_type(element_type) {}

  Type *GetElementType() const { return &m_element_type.Get(); }

  /// Complex-float when the element is a floating-point type, else
  /// complex-integer.
  bool IsFloat() const {
    return m_element_type.Get().GetEncoding() == lldb::eEncodingIEEE754;
  }

  // A complex value is two contiguous components of the element type. Computed
  // on demand rather than stored (the element size may only be known later).
  std::optional<uint64_t> GetByteSize() const override {
    if (std::optional<uint64_t> element_size =
            m_element_type.Get().GetByteSize())
      return *element_size * 2;
    return std::nullopt;
  }
  std::optional<uint64_t> GetAlignmentInBits() const override {
    return m_element_type.Get().GetAlignmentInBits();
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

  /// \p param_names holds the declared name of each parameter in \p params
  /// (an empty Identifier for an unnamed one); it may be shorter than
  /// \p params, in which case the remaining parameters are unnamed.
  /// \p use_void_for_empty_params: see UseVoidForEmptyParams.
  FunctionType(TypeRef return_type, std::vector<TypeRef> params,
               std::vector<Identifier> param_names, bool is_variadic,
               bool use_void_for_empty_params)
      : m_return_type(return_type), m_params(std::move(params)),
        m_param_names(std::move(param_names)), m_is_variadic(is_variadic),
        m_use_void_for_empty_params(use_void_for_empty_params) {
    assert(m_param_names.size() <= m_params.size() &&
           "more parameter names than parameters");
    m_param_names.resize(m_params.size());
  }

  bool IsFunction() const override { return true; }

  /// The declared return type. Never null: a function returning nothing
  /// returns the `void` builtin.
  Type *GetReturnType() const override { return &m_return_type.Get(); }

  uint32_t GetNumParameters() const override { return m_params.size(); }
  Type *GetParameterAtIndex(uint32_t idx) const override {
    if (idx < m_params.size())
      return &m_params[idx].Get();
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

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassFunction;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeIsFuncPrototype | lldb::eTypeHasValue;
  }

private:
  TypeRef m_return_type;
  std::vector<TypeRef> m_params;
  // Parallel to m_params: the declared name of each parameter (empty Identifier
  // for an unnamed parameter). Kept separate so the common size/layout queries
  // that only need parameter *types* don't touch it.
  std::vector<Identifier> m_param_names;
  bool m_is_variadic;
  bool m_use_void_for_empty_params;
};

} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_C_H
