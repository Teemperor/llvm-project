//===-- Type.h ------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This header declares the basic classes needed for the Type interface.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_H

#include <cassert>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "Identifier.h"

namespace lldb_private {
namespace clike {

class Context;
class Namespace;
class Type;

struct TemplateArgument;
struct MemberFunction;
struct StaticDataMember;
struct ObjCMethod;

/// A reference to another Type.
///
/// The other type can belong to another context.
///
/// This is currently a wrapper around a Type pointer. In the future this could
/// also reference types that are no longer in memory (e.g., because they have
/// been deallocated to free up space).
class TypeRef {
public:
  explicit TypeRef(Type &type) : m_type(&type) {}

  /// The referenced type.
  Type &Get() const { return *m_type; }

private:
  Type *m_type;
};

static_assert(sizeof(TypeRef) == sizeof(void *),
              "TypeRef is expected to be pointer-sized!");

/// A single data member of a record type.
class Field {
public:
  /// \p bitfield_bit_size is the width of a bitfield member, or zero if this is
  /// not a bitfield (in which case \p bitfield_bit_offset must be zero too).
  Field(Identifier name, TypeRef type, uint64_t byte_offset,
        uint32_t bitfield_bit_size = 0, uint32_t bitfield_bit_offset = 0)
      : m_name(name), m_type(type), m_byte_offset(byte_offset),
        m_bitfield_bit_size(bitfield_bit_size),
        m_bitfield_bit_offset(bitfield_bit_offset) {
    assert((IsBitfield() || bitfield_bit_offset == 0) &&
           "bit offset given for a member that is not a bitfield");
  }

  Identifier GetName() const { return m_name; }

  /// The type of this member.
  TypeRef GetType() const { return m_type; }

  /// Offset of this member (or, for a bitfield, of its storage unit) from the
  /// start of the record, in bytes.
  uint64_t GetByteOffset() const { return m_byte_offset; }

  bool IsBitfield() const { return m_bitfield_bit_size != 0; }

  /// The width of this bitfield in bits. Only valid for a bitfield.
  uint32_t GetBitfieldBitSize() const {
    assert(IsBitfield() && "not a bitfield");
    return m_bitfield_bit_size;
  }

  /// The offset of this bitfield's first bit within the storage unit at
  /// GetByteOffset(). Only valid for a bitfield.
  uint32_t GetBitfieldBitOffset() const {
    assert(IsBitfield() && "not a bitfield");
    return m_bitfield_bit_offset;
  }

private:
  Identifier m_name;
  TypeRef m_type;
  uint64_t m_byte_offset;
  /// Zero means this is not a bitfield.
  uint32_t m_bitfield_bit_size;
  uint32_t m_bitfield_bit_offset;
};

/// A direct base class of a C++ class type. Objective-C reuses this to model an
/// interface's superclass (see ObjCInterfaceType), which is why it lives here
/// rather than in TypeCpp.h.
class BaseClass {
public:
  /// \p vbase_offset_offset may only be given for a virtual base (see
  /// GetVBaseOffsetOffset).
  BaseClass(TypeRef type, uint64_t byte_offset, bool is_virtual = false,
            std::optional<uint64_t> vbase_offset_offset = std::nullopt)
      : m_type(type), m_byte_offset(byte_offset), m_is_virtual(is_virtual),
        m_vbase_offset_offset(vbase_offset_offset) {
    assert((is_virtual || !vbase_offset_offset) &&
           "vbase offset offset given for a non-virtual base");
  }

  /// The base class type.
  TypeRef GetType() const { return m_type; }

  /// Offset of the base class subobject from the start of the derived record.
  uint64_t GetByteOffset() const { return m_byte_offset; }

  /// True if this is a virtual base class.
  bool IsVirtual() const { return m_is_virtual; }

  /// For virtual bases under the Itanium ABI. The byte offset that,
  /// subtracted from the derived object's vtable pointer, yields the address of
  /// the slot holding this virtual base's offset. That is, the base subobject
  /// lives at `obj + *(int*)(*(void**)obj - vbase_offset_offset)`. Extracted
  /// from the base's DWARF DW_AT_data_member_location expression (the standard
  /// `DW_OP_dup DW_OP_deref DW_OP_constu N DW_OP_minus DW_OP_deref DW_OP_plus`
  /// form). std::nullopt when not a virtual base or the expression didn't
  /// match, in which case consumers fall back to GetByteOffset().
  std::optional<uint64_t> GetVBaseOffsetOffset() const {
    return m_vbase_offset_offset;
  }

private:
  TypeRef m_type;
  uint64_t m_byte_offset;
  bool m_is_virtual;
  std::optional<uint64_t> m_vbase_offset_offset;
};

/// A single (name, value) constant of an enumeration (see EnumType).
class Enumerator {
public:
  Enumerator(Identifier name, uint64_t value) : m_name(name), m_value(value) {}

  Identifier GetName() const { return m_name; }

  /// The constant's value, stored as raw bits; interpret as signed when the
  /// enum's underlying type is signed.
  uint64_t GetValue() const { return m_value; }

private:
  Identifier m_name;
  uint64_t m_value;
};

/// The ARMv8.3 pointer-authentication schema of a
/// `T *__ptrauth(key, address_discriminated, extra_discriminator)` pointer
/// (see PtrAuthType).
class PtrAuthQualifier {
public:
  PtrAuthQualifier(unsigned key, bool address_discriminated,
                   unsigned extra_discriminator)
      : m_key(key), m_address_discriminated(address_discriminated),
        m_extra_discriminator(extra_discriminator) {
    assert(extra_discriminator <= 0xFFFF &&
           "extra discriminator must fit in 16 bits");
  }

  /// The pointer-authentication key (a small integer, e.g. 2 for the data key).
  unsigned GetKey() const { return m_key; }

  /// True if the pointer is signed with address diversity (the storage address
  /// participates in the discriminator).
  bool IsAddressDiscriminated() const { return m_address_discriminated; }

  /// The extra (constant) discriminator blended into the signature.
  unsigned GetExtraDiscriminator() const { return m_extra_discriminator; }

private:
  unsigned m_key;
  bool m_address_discriminated;
  unsigned m_extra_discriminator;
};

/// Where a named type (a record, typedef or enum) is declared: the namespace
/// that encloses it and its unqualified spelling. Together with the template
/// arguments these rebuild the (inline-namespace-aware) display name; see
/// Type::GetDeclContext and Type::GetUnqualifiedName. Both are optional: a
/// default-constructed DeclScope describes a type in the global namespace
/// whose display name is just its qualified name.
class DeclScope {
public:
  DeclScope() = default;
  /// \p unqualified_name must be interned in the IdentifierMap of the Context
  /// that owns the type.
  DeclScope(const Namespace *decl_context, Identifier unqualified_name)
      : m_decl_context(decl_context), m_unqualified_name(unqualified_name) {}

  /// The enclosing namespace, or null for the global namespace.
  const Namespace *GetDeclContext() const { return m_decl_context; }

  /// The unqualified spelling, or empty when unknown.
  Identifier GetUnqualifiedName() const { return m_unqualified_name; }

private:
  const Namespace *m_decl_context = nullptr;
  Identifier m_unqualified_name;
};

/// The base class of all Types in the Clike TypeSystem.
///
/// This class implements an API that is called by TypeSystemClike to respond to
/// TypeSystem queries. It is also the base class for opaque_compiler_type_t.
///
/// TypeSystemClike never inspects which concrete kind a Type is: every query
/// that depends on the kind is a virtual method here, defaulting to the answer
/// for a type it doesn't apply to ("no pointee", "no enumerators", ...), and
/// overridden by the kinds it does apply to.
///
/// A Type is immutable once created, with the exception of records: those are
/// often created from a forward declaration and filled in later during lazy
/// completion. That mutation is gated behind Context (see RecordType).
class Type : public llvm::RTTIExtends<Type, llvm::RTTIRoot> {
public:
  /// LLVM-style RTTI support.
  static char ID;

  virtual ~Type() = default;

  /// The Context that owns this type.
  Context &GetOwningContext() const {
    assert(m_context && "every type is owned by the Context that created it");
    return *m_context;
  }

  /// The type's qualified name.
  virtual Identifier GetName() const { return Identifier(); }

  /// The namespace this type is declared in (null for the global namespace,
  /// and for a structural type, which has no declaration context). Used to
  /// build the qualified display name while skipping inline namespaces. See
  /// clike::Namespace.
  virtual const Namespace *GetDeclContext() const { return nullptr; }

  /// The unqualified spelling as written in the debug info (e.g. `string` or
  /// `vector<int, std::allocator<int>>`), without any namespace qualification.
  /// Used, together with GetDeclContext() and the template arguments, to build
  /// the (possibly simplified) display name. Empty for a structural type.
  virtual Identifier GetUnqualifiedName() const { return Identifier(); }

  /// The size of a value of this type, in bytes, or std::nullopt when unknown
  /// (e.g. an incomplete record, or a function type). Most structural types
  /// (pointers, references, arrays, sugar, ...) either derive their size from
  /// another type or share the target's fixed pointer size, so the storage for
  /// an explicit size is NOT held here -- where every Type would pay 8 bytes
  /// for it -- but only on the kinds that need it (see ByteSizedType and the
  /// pointer/array/complex overrides in the per-language headers).
  virtual std::optional<uint64_t> GetByteSize() const { return std::nullopt; }

  /// This type's alignment in bits, or std::nullopt when it cannot be
  /// determined.
  ///
  /// The model doesn't record alignment for most types, so by default it is
  /// derived from the size: for a scalar the derived alignment is its size; for
  /// an aggregate it is the largest power of two that divides the size (the
  /// natural alignment of the standard-layout types debug info produces),
  /// capped at 8 bytes -- the fundamental alignment on the supported targets.
  /// A pointer/reference overrides this with the target's pointer alignment,
  /// and a record prefers an alignment the debug info recorded explicitly (see
  /// RecordType::GetExplicitAlignmentInBits).
  virtual std::optional<uint64_t> GetAlignmentInBits() const;

  /// Peel "sugar" (typedef/cv-qualifier/elaborated/ptrauth) off this type to
  /// reach its canonical type. Mirrors clang's RemoveWrappingTypes: queries
  /// about layout and children should see through aliases and qualifiers, so
  /// most of TypeSystemClike's query methods call this first. Sugar overrides it
  /// to forward to what it wraps (see SugarType); every other kind is already
  /// canonical and returns itself.
  ///
  /// A family of methods that disagree about desugaring causes infinite loops
  /// (FormatManager) or "not a member" bugs, so when in doubt, desugar.
  virtual Type *Desugar() { return this; }
  const Type *Desugar() const {
    return const_cast<Type *>(this)->Desugar();
  }

  /// True if this type has members (a struct/class/union).
  virtual bool IsAggregate() const { return false; }

  /// True if all the information about this type is available. Non-aggregate
  /// types are always complete; records may start out as forward declarations.
  virtual bool IsComplete() const { return true; }

  virtual lldb::Encoding GetEncoding() const { return lldb::eEncodingInvalid; }
  virtual lldb::Format GetFormat() const { return lldb::eFormatDefault; }
  virtual lldb::TypeClass GetTypeClass() const { return lldb::eTypeClassOther; }
  virtual uint32_t GetTypeInfo() const { return 0; }

  /// The least specific source language this type can be expressed in. This
  /// is what routes a value to a language's formatters and runtime: C++ for a
  /// record (so e.g. the libc++ container formatters are consulted),
  /// Objective-C for an interface or an object pointer (so the ObjC runtime
  /// resolves its dynamic type), and C for everything else. Mirrors
  /// TypeSystemClang::GetMinimumLanguage. Callers desugar first.
  virtual lldb::LanguageType GetMinimumLanguage() const {
    return lldb::eLanguageTypeC;
  }

  /// \name Sugar
  /// Queries that look at (part of) the sugar chain rather than through it.
  /// @{

  /// Peel any elaborated display sugar (see ElaboratedType) off this type,
  /// stopping at the first type that carries real meaning. Mirrors
  /// TypeSystemClang's RemoveWrappingTypes({Typedef}): unlike a full Desugar()
  /// this keeps a typedef, since a typedef named through a qualifier
  /// (`GlobalTypedef::V`) is still a typedef and the alias name is meaningful.
  virtual Type *StripElaborated() { return this; }
  const Type *StripElaborated() const {
    return const_cast<Type *>(this)->StripElaborated();
  }

  /// Peel the cv-qualifier sugar (see CVQualifiedType) at the top of this type,
  /// including stacked qualifiers, but no other sugar: typedefs and elaborated
  /// spellings are meaningful to e.g. data formatters and are kept.
  virtual Type *StripCVQualifiers() { return this; }
  const Type *StripCVQualifiers() const {
    return const_cast<Type *>(this)->StripCVQualifiers();
  }

  /// The CVR qualifier mask applying to this type, using clang's bit layout
  /// (Const = 0x1, Restrict = 0x2, Volatile = 0x4) so the result can be handed
  /// straight to clang::Qualifiers::fromCVRMask. DWARF nests one qualifier per
  /// DIE, so `const volatile T` is modeled as two stacked CVQualifiedType
  /// nodes; the mask ORs together the whole chain of cv-sugar at the top of
  /// this type (as a single clang `const volatile T` would report it).
  virtual unsigned GetCVRQualifiers() const { return 0; }

  /// True if this type is itself const-qualified sugar. Unlike
  /// GetCVRQualifiers this only looks at the outermost qualifier.
  virtual bool IsConst() const { return false; }

  /// The pointer-authentication qualifier applying to this type, looking
  /// through any sugar wrapped around it (a `__ptrauth` qualifier sits on the
  /// outermost declarator, so only see-through sugar can hide it), or
  /// std::nullopt if there is none.
  virtual std::optional<PtrAuthQualifier> GetPtrAuthQualifier() const {
    return std::nullopt;
  }

  /// For a typedef: the type it aliases. Null for everything else (including
  /// sugar wrapped around a typedef -- see StripElaborated).
  virtual Type *GetTypedefedType() const { return nullptr; }
  /// @}

  /// \name Builtins
  /// @{

  /// True for `void`. Since a `void *` points at the `void` builtin rather
  /// than at nothing (see TypeRef), this -- not a null check -- is how the
  /// handful of places that must treat an opaque pointer specially recognize
  /// one. See also the null-tolerant, desugaring clike::IsVoid.
  virtual bool IsVoid() const { return false; }

  /// True for the narrow character types (char/signed char/unsigned char, but
  /// not wchar_t), matching clang's isCharType().
  virtual bool IsChar() const { return false; }

  /// True for an integer type: a builtin integer (not `std::nullptr_t`, which
  /// has an unsigned pointer-width encoding but is not an integer) or an
  /// enumeration with an integer encoding. Pointers report an unsigned
  /// encoding for value extraction but are NOT integers, or e.g. DIL's
  /// array-subscript index checking would accept a pointer index. The
  /// signedness comes from GetEncoding().
  virtual bool IsInteger() const { return false; }

  /// True for a type the C++ integral promotions apply to ([conv.prom]):
  /// bool, the character types, the integer types whose conversion rank is
  /// below int, and unscoped enumerations.
  virtual bool IsPromotableInteger() const { return false; }

  /// The lldb::BasicType this type corresponds to, or eBasicTypeInvalid for
  /// anything that isn't one of the enumerated builtins.
  virtual lldb::BasicType GetBasicTypeEnumeration() const {
    return lldb::eBasicTypeInvalid;
  }
  /// @}

  /// \name Pointers and references
  /// @{

  /// True for a pointer (including an Apple "blocks" pointer).
  virtual bool IsPointer() const { return false; }

  /// True for an Apple "blocks" pointer (`int (^)(int)`).
  virtual bool IsBlockPointer() const { return false; }

  /// True for a function pointer (`void (*)(int)`), matching clang's
  /// isFunctionPointerType() -- see PointerType::IsFunctionPointer.
  virtual bool IsFunctionPointer() const { return false; }

  /// True for an lvalue (`T &`) or rvalue (`T &&`) reference.
  virtual bool IsReference() const { return false; }

  /// True for an rvalue reference (`T &&`).
  virtual bool IsRValueReference() const { return false; }

  /// For a pointer, the type it points to; for a reference, the type it refers
  /// to. Null for everything else (in particular, a pointer-to-member has no
  /// pointee in this sense, matching what TypeSystemClike reports for it).
  virtual Type *GetPointeeType() const { return nullptr; }

  /// True for a pointer to a non-static member function (`R (C::*)(Args...)`).
  virtual bool IsMemberFunctionPointer() const { return false; }

  /// True for a pointer to a non-static data member (`T C::*`).
  virtual bool IsMemberDataPointer() const { return false; }

  /// For a pointer or reference whose *children* are the pointee's members
  /// rather than a single deref child: the pointee to splice them in from.
  /// Null for everything else, and for a pointer/reference that keeps its
  /// single deref child.
  ///
  /// Pointers are transparent, mirroring TypeSystemClang: expanding `ptr`
  /// splices in the pointee aggregate's members instead of showing one deref
  /// child. This is what the shared libc++ synthetic-child providers expect
  /// (they call GetChildMemberWithName/GetChildAtIndex directly on
  /// pointer-typed node values). Only an *already-complete* aggregate is
  /// spliced, so that merely counting a pointer's children doesn't force
  /// completion of an otherwise-lazy pointee; a pointer to an
  /// incomplete/non-aggregate pointee (and `void *`) keeps its deref child.
  ///
  /// An Objective-C object is only ever accessed through a pointer (there is
  /// no by-value ObjC object), so a pointer to an ObjC interface is always
  /// transparent -- see PointerType's override.
  ///
  /// The methods answering child queries (GetNumChildren,
  /// GetChildCompilerTypeAtIndex, ...) must agree on this exactly, or child
  /// counts and child indices describe different layouts; hence the single
  /// definition here.
  virtual Type *GetTransparentChildPointee() { return nullptr; }

  /// For a pointer or reference through which a *named* member is directly
  /// addressable (`ptr->member`, `ref.member`): the pointee to look the name
  /// up in. Null for everything else.
  ///
  /// This is the by-name counterpart of GetTransparentChildPointee, and is
  /// deliberately more permissive: a name lookup may complete the pointee as a
  /// side effect of resolving the name, so it does not require the pointee to
  /// be complete up front (an incomplete aggregate simply has no members to
  /// find).
  virtual Type *GetNamedMemberPointee() { return nullptr; }

  /// For a pointer or reference whose dynamic (most-derived) type may differ
  /// from its static pointee: the static pointee whose dynamic type the
  /// language runtime should resolve. Null if this is not a possible dynamic
  /// type. Mirrors TypeSystemClang::IsPossibleDynamicType:
  ///  - a pointer to an Objective-C object (when \p check_objc),
  ///  - a pointer or reference to a polymorphic C++ class (when
  ///    \p check_cplusplus; see IsDynamicClass), and
  ///  - a `void *` regardless of either flag (an opaque pointer may point at
  ///    either kind of object).
  ///
  /// \p complete is invoked on a record whose polymorphism is only known once
  /// it is complete; completion needs the SymbolFile, which the type model
  /// doesn't have, so the caller supplies it.
  virtual Type *
  GetDynamicTypeCandidate(bool check_cplusplus, bool check_objc,
                          llvm::function_ref<void(Type *)> complete) {
    return nullptr;
  }

  /// True if this is a polymorphic C++ class, i.e. one whose objects carry a
  /// vtable pointer through which their dynamic type can be found. Completes
  /// the class first via \p complete, since the vtable fact is only known
  /// after completion (mirroring clang's GetCompleteType() ->
  /// isDynamicClass()). See GetDynamicTypeCandidate.
  virtual bool IsDynamicClass(llvm::function_ref<void(Type *)> complete) {
    return false;
  }
  /// @}

  /// \name Arrays
  /// @{

  /// For an array (including a vector, see IsVector): its element type. Null
  /// for everything else.
  virtual Type *GetArrayElementType() const { return nullptr; }

  /// For an array: its number of elements. std::nullopt for an array of
  /// unknown bound, and for everything that isn't an array.
  virtual std::optional<uint64_t> GetNumElements() const {
    return std::nullopt;
  }

  /// True if this is a GCC/Clang vector type: laid out like an array, but
  /// not an array type as far as LLDB is concerned (matching TypeSystemClang,
  /// where a Vector/ExtVector is distinct from ConstantArray).
  virtual bool IsVector() const { return false; }

  /// The UID of the debug-info entry that produced this type, or UINT64_MAX
  /// (i.e. LLDB_INVALID_UID) when none was recorded. Only an array records it,
  /// so the symbol file can re-resolve a runtime (variable-length) bound.
  virtual uint64_t GetDIEUID() const { return UINT64_MAX; }
  /// @}

  /// \name Functions
  /// @{

  /// True for a function type.
  virtual bool IsFunction() const { return false; }

  /// For a function: its declared return type (never null; a function
  /// returning nothing returns the `void` builtin). Null for everything else.
  virtual Type *GetReturnType() const { return nullptr; }

  /// For a function: its parameters. For a member function these exclude the
  /// implicit object (`this`) parameter. None for everything else.
  virtual uint32_t GetNumParameters() const { return 0; }
  virtual Type *GetParameterAtIndex(uint32_t idx) const { return nullptr; }
  /// @}

  /// \name Enumerations
  /// @{

  /// For an enumeration: the integer type backing it. Null for everything
  /// else.
  virtual Type *GetEnumerationIntegerType() const { return nullptr; }

  /// True for a scoped enumeration (`enum class`), whose enumerators are not
  /// in the enclosing scope.
  virtual bool IsScopedEnumeration() const { return false; }

  /// For an enumeration: its named constants. Empty for everything else.
  virtual llvm::ArrayRef<Enumerator> GetEnumerators() const { return {}; }
  /// @}

  /// \name Records
  /// Members of a record type. Template arguments, member functions and static
  /// data members are C++-only and Objective-C methods ObjC-only, so their
  /// storage lives on the respective kinds; the accessors are declared here so
  /// that every kind answers them ("none" by default).
  /// @{

  /// True for a record type: a C struct/union, a C++ class, or an Objective-C
  /// interface.
  virtual bool IsRecord() const { return false; }

  /// True for an Objective-C class type (`@interface`).
  virtual bool IsObjCInterface() const { return false; }

  /// True if this record is a `union` (all members share offset 0).
  virtual bool IsUnion() const { return false; }

  /// True if this record was declared with the `class` keyword (as opposed to
  /// `struct`). See RecordType::IsClassKeyword.
  virtual bool IsClassKeyword() const { return false; }

  /// True if this record is an anonymous struct/union. See
  /// RecordType::IsAnonymousStructOrUnion.
  virtual bool IsAnonymousStructOrUnion() const { return false; }

  /// Members of a record type. Empty for non-records.
  virtual uint32_t GetNumFields() const { return 0; }
  virtual const Field *GetFieldAtIndex(uint32_t idx) const { return nullptr; }

  /// Direct base classes of a C++ class type. Empty for everything else
  /// (including plain C structs, which never have base classes).
  virtual uint32_t GetNumBaseClasses() const { return 0; }
  virtual const BaseClass *GetBaseClassAtIndex(uint32_t idx) const {
    return nullptr;
  }

  /// True if this is a polymorphic C++ class -- i.e. it (or one of its base
  /// classes) has a vtable. Only ClassType can be polymorphic. This drives
  /// C++ dynamic-type detection (IsPossibleDynamicType / IsPolymorphicClass):
  /// the object's vtable pointer is followed to its RTTI to find the
  /// most-derived type. Unlike member functions (parsed lazily), this fact is
  /// recorded eagerly during record completion from the artificial `_vptr$`
  /// member / a virtual base, so it is reliable without forcing method parsing.
  virtual bool IsPolymorphic() const { return false; }

  /// True if this record is a class-template instantiation, regardless of how
  /// many template arguments it has. This is distinct from
  /// `GetNumTemplateArguments() > 0`: a specialization of a variadic template
  /// over an *empty* pack (e.g. `TypePack<>`) has zero arguments yet is still a
  /// template instantiation and must print an (empty) `<>` argument list.
  virtual bool IsTemplateInstantiation() const { return false; }

  /// Template arguments, if this record is a class-template instantiation.
  virtual uint32_t GetNumTemplateArguments() const { return 0; }
  virtual const TemplateArgument *GetTemplateArgumentAtIndex(uint32_t idx) const {
    return nullptr;
  }

  /// True once this record's member functions (or Objective-C methods) have
  /// been parsed. They are only needed by the expression evaluator, so --
  /// unlike fields and base classes -- they are parsed lazily, in a separate
  /// step after the record is otherwise complete. A complete record may still
  /// have unparsed member functions. Anything that isn't a record has no
  /// member functions to parse, so reports true.
  virtual bool AreMemberFunctionsParsed() const { return true; }

  /// Member functions of this record (used to resolve/call methods from an
  /// expression).
  virtual uint32_t GetNumMemberFunctions() const { return 0; }
  virtual const MemberFunction *GetMemberFunctionAtIndex(uint32_t idx) const {
    return nullptr;
  }

  /// Objective-C methods of this interface (used to build ObjCMethodDecls so
  /// the expression parser can type-check and lower a message send).
  virtual uint32_t GetNumObjCMethods() const { return 0; }
  virtual const ObjCMethod *GetObjCMethodAtIndex(uint32_t idx) const {
    return nullptr;
  }

  /// Static data members of this record (`static int s;`). Used by the
  /// expression evaluator to resolve `record.s` / `Record::s` and, for a
  /// constant integral member, to fold its value.
  virtual uint32_t GetNumStaticDataMembers() const { return 0; }
  virtual const StaticDataMember *GetStaticDataMemberAtIndex(uint32_t idx) const {
    return nullptr;
  }

  /// Look up a type declared directly inside this record (a nested typedef,
  /// class, union or enum) by its unqualified name. Returns null if there is no
  /// such nested type. Data formatters use this to reach a container's internal
  /// helper types (e.g. a tree's "__node_pointer").
  virtual Type *GetNestedTypeWithName(llvm::StringRef name) const {
    return nullptr;
  }

  /// Number of types declared directly inside this record (nested typedefs,
  /// classes, unions or enums). Used by the expression evaluator to decide
  /// whether a generated record needs a name-lookup callback for
  /// `Record::Nested`; the types themselves are looked up by name via
  /// GetNestedTypeWithName.
  virtual uint32_t GetNumNestedTypes() const { return 0; }

  /// If this is a record that is a Homogeneous Floating-point/Vector
  /// Aggregate, the element type every one of its fields has; null otherwise.
  /// \p num_fields receives the field count on success (0 otherwise).
  ///
  /// A record qualifies when it has no base classes, is not polymorphic, and
  /// its *direct* fields are all either the same scalar floating-point type
  /// (HFA) or all the same vector type with matching bit width (HVA) -- never a
  /// mix of the two, and never any other kind of field. In particular there is
  /// no recursion into nested aggregate fields: a struct-typed field always
  /// disqualifies the record, matching clang's
  /// isFloatingType()/isVectorType() checks. Port of
  /// TypeSystemClang::IsHomogeneousAggregate; callers must complete the record
  /// first.
  virtual Type *GetHomogeneousAggregateBase(uint32_t &num_fields) const {
    num_fields = 0;
    return nullptr;
  }

  /// Whether this (possibly sugared) type is a record with any data members,
  /// considering base classes recursively. A vtable pointer is not a field, so
  /// a polymorphic-but-otherwise-empty class answers false. Mirrors
  /// TypeSystemClang::RecordHasFields; this drives whether an empty base class
  /// is omitted from a value's children (the `omit_empty_base_classes` flag
  /// threaded through the child-index queries).
  ///
  /// \p complete is invoked on each (lazily-parsed) record before it is
  /// inspected, so its field count is known. Completion isn't something the
  /// type model can do on its own -- it needs the SymbolFile -- so the caller
  /// supplies it.
  bool HasFields(llvm::function_ref<void(Type *)> complete);
  /// @}

private:
  // Set exactly once, when the Context takes ownership of the type -- which is
  // also the only way a type is created. Gated to Context like the structural
  // mutators on the concrete kinds.
  friend class Context;
  void SetOwningContext(Context &context) { m_context = &context; }

  Context *m_context = nullptr;
};

/// Null-tolerant wrapper around Type::Desugar(), for the many call sites that
/// desugar a `Type *` that may not exist (a lookup that found nothing, a
/// pointee peeled off a type that turned out not to be a pointer).
inline Type *Desugar(Type *t) { return t ? t->Desugar() : nullptr; }
inline const Type *Desugar(const Type *t) { return t ? t->Desugar() : nullptr; }

/// True if \p t is `void`, looking through sugar. Null-tolerant counterpart of
/// Type::IsVoid.
inline bool IsVoid(const Type *t) {
  const Type *desugared = Desugar(t);
  return desugared && desugared->IsVoid();
}

/// Mixin adding the name/declaration-context storage that Type declares (as
/// virtual accessors defaulting to "no name") but does not itself store --
/// see the comment on Type for why. Inherited by every named type (a record,
/// typedef or enum) over whichever base it would otherwise have (`Type`
/// directly, or `SugarType` for a typedef), so the storage and its accessor
/// overrides are written once instead of copy-pasted per class.
template <typename Base> class NamedType : public Base {
public:
  /// \p name is the qualified name, \p scope where the type is declared; any
  /// remaining arguments are forwarded to \p Base.
  template <typename... Args>
  NamedType(Identifier name, DeclScope scope, Args &&...args)
      : Base(std::forward<Args>(args)...), m_name(name),
        m_unqualified_name(scope.GetUnqualifiedName()),
        m_decl_context(scope.GetDeclContext()) {}

  Identifier GetName() const override { return m_name; }
  const Namespace *GetDeclContext() const override { return m_decl_context; }
  Identifier GetUnqualifiedName() const override { return m_unqualified_name; }

private:
  Identifier m_name;
  Identifier m_unqualified_name;
  const Namespace *m_decl_context = nullptr;
};

/// Mixin adding explicit byte-size storage for the type kinds that carry a
/// size of their own (a builtin, a record, an enum). Type declares GetByteSize
/// as a virtual defaulting to "unknown" but stores nothing itself (see the
/// comment on Type); this adds the storage and the accessor override once,
/// composed over whichever base the type would otherwise have. Structural
/// types whose size is derived (arrays, complex, sugar) or a small fixed
/// pointer width (see PointerType et al.) do NOT use this.
template <typename Base> class ByteSizedType : public Base {
public:
  /// \p byte_size is std::nullopt when unknown; any remaining arguments are
  /// forwarded to \p Base.
  template <typename... Args>
  explicit ByteSizedType(std::optional<uint64_t> byte_size, Args &&...args)
      : Base(std::forward<Args>(args)...),
        m_byte_size(byte_size.value_or(kNoByteSize)) {}

  std::optional<uint64_t> GetByteSize() const override {
    return m_byte_size == kNoByteSize ? std::nullopt
                                      : std::optional<uint64_t>(m_byte_size);
  }

protected:
  /// Only for a record, whose size may only be learned when it is completed
  /// (see RecordType).
  void SetByteSize(std::optional<uint64_t> byte_size) {
    m_byte_size = byte_size.value_or(kNoByteSize);
  }

private:
  // A byte size can be any 64-bit value in principle, but never in practice
  // (no real type is 2^64-1 bytes), so steal that value as the "unset"
  // sentinel instead of paying for std::optional's separate bool.
  static constexpr uint64_t kNoByteSize = UINT64_MAX;
  uint64_t m_byte_size = kNoByteSize;
};

/// Common base for C/C++ record types (struct/class/union). Owns the data
/// members and the forward-declaration/completion state that every record
/// shares. C++-only information (such as base classes) lives on ClassType, so
/// a plain StructType never reserves storage for it.
///
/// A record is the one kind of type that is not immutable: it is often created
/// from a forward declaration, and its members (and a size the declaration
/// didn't carry) are only filled in when it is completed. That mutation is
/// gated: only Context can perform it, and Context is only reachable through a
/// clike::Builder.
class RecordType : public llvm::RTTIExtends<RecordType, NamedType<ByteSizedType<Type>>> {
public:
  static char ID;

  // The flag members below are packed bit-fields with no default member
  // initializers (those are a C++20 feature; LLVM builds as C++17), so they
  // are given their defaults here.
  explicit RecordType(Identifier name = Identifier(), DeclScope scope = {},
                      std::optional<uint64_t> byte_size = std::nullopt,
                      bool is_union = false, bool is_class_keyword = false)
      : RTTIExtends(name, scope, byte_size), m_complete(false),
        m_is_union(is_union), m_is_class_keyword(is_class_keyword),
        m_is_anonymous_struct_union(false), m_member_functions_parsed(false),
        m_arg_passing(ArgPassingKind::Unspecified) {}

  bool IsAggregate() const override { return true; }
  bool IsComplete() const override { return m_complete; }
  bool IsRecord() const override { return true; }
  lldb::LanguageType GetMinimumLanguage() const override {
    return lldb::eLanguageTypeC_plus_plus;
  }

  /// An explicitly-specified alignment in bits (from `DW_AT_alignment`, e.g.
  /// `alignas(128)`), if the debug info recorded one. Only a record can carry
  /// one, so unlike name/decl-context this isn't shared via NamedType. Most
  /// callers want GetAlignmentInBits instead, which falls back to deriving an
  /// alignment from the size when none was recorded.
  std::optional<uint64_t> GetExplicitAlignmentInBits() const {
    return m_explicit_alignment_in_bits;
  }

  /// Prefers the explicitly-recorded alignment (which the size-derived
  /// heuristic cannot recover), else falls back to Type's derivation.
  std::optional<uint64_t> GetAlignmentInBits() const override;

  /// True if this record is a `union` (all members share offset 0). Needed so
  /// the type can be reconstructed with the correct tag kind.
  bool IsUnion() const override { return m_is_union; }

  /// True if this record was declared with the `class` keyword (as opposed to
  /// `struct`). In a C++ translation unit both are modeled as a ClassType (the
  /// struct/class keyword does not change layout), so this preserves the source
  /// tag keyword purely for naming an *unnamed* record ("(unnamed class)" vs
  /// "(unnamed struct)"), matching clang's TagDecl printing.
  bool IsClassKeyword() const override { return m_is_class_keyword; }

  /// True if this record is an anonymous struct/union: an *unnamed* record that
  /// is embedded as an *unnamed* member of its enclosing record, so its members
  /// are injected into the enclosing scope (e.g. `struct { int y; };`). This is
  /// distinct from a merely unnamed record that is given a member name
  /// (`struct { int x; } named;`), which is not anonymous. Mirrors clang's
  /// RecordDecl::isAnonymousStructOrUnion(); set by the DWARF parser.
  bool IsAnonymousStructOrUnion() const override {
    return m_is_anonymous_struct_union;
  }

  /// The record this type is anonymously embedded in as an unnamed member (set
  /// alongside IsAnonymousStructOrUnion()); null if this is not an anonymous
  /// struct/union. Since an anonymous struct/union has no name of its own to
  /// qualify (see DeclContext, which only models enclosing namespaces, never
  /// classes), this is how its display name recovers the enclosing class scope
  /// (e.g. `MySock::(anonymous union)`), mirroring clang's
  /// NamedDecl::printNestedNameSpecifier walking the DeclContext chain (which,
  /// unlike this model, treats a RecordDecl as a DeclContext too).
  const RecordType *GetAnonymousParent() const { return m_anonymous_parent; }

  /// How this record is passed as a function argument / returned, derived from
  /// DWARF's DW_AT_calling_convention (DW_CC_pass_by_value /
  /// DW_CC_pass_by_reference). This governs the ABI clang uses for calls that
  /// pass or return the record by value (see ClangASTGenerator), which matters
  /// for expression evaluation calling such functions.
  enum class ArgPassingKind : uint8_t {
    /// No DW_AT_calling_convention was recorded; let clang decide.
    Unspecified,
    /// DW_CC_pass_by_value: the record has a trivial-for-call ABI even if it
    /// has non-trivial special members (clang: setHasTrivialSpecialMemberForCall).
    PassByValue,
    /// DW_CC_pass_by_reference: the record cannot be passed in registers
    /// (clang: RecordArgPassingKind::CannotPassInRegs).
    CannotPassInRegs,
  };
  ArgPassingKind GetArgPassingKind() const { return m_arg_passing; }

  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsStructUnion;
  }

  uint32_t GetNumFields() const override { return m_fields.size(); }
  const Field *GetFieldAtIndex(uint32_t idx) const override {
    if (idx < m_fields.size())
      return &m_fields[idx];
    return nullptr;
  }

  /// Member functions are only needed by the expression evaluator (to call
  /// methods), so they are parsed lazily -- see Type::AreMemberFunctionsParsed.
  /// This avoids pulling in every method's signature (and the types it
  /// references) merely to inspect a value of this type.
  bool AreMemberFunctionsParsed() const override {
    return m_member_functions_parsed;
  }

  Type *GetNestedTypeWithName(llvm::StringRef name) const override {
    for (const auto &entry : m_nested_types)
      if (entry.first.GetName() == name)
        return &entry.second.Get();
    return nullptr;
  }

  uint32_t GetNumNestedTypes() const override { return m_nested_types.size(); }

  Type *GetHomogeneousAggregateBase(uint32_t &num_fields) const override;

private:
  // Structural mutation happens after creation (during lazy completion), so it
  // is gated: only Context can perform it, and Context is only reachable
  // through a clike::Builder.
  friend class Context;
  void SetIsComplete(bool complete) { m_complete = complete; }
  /// A record created from a forward declaration (which carries no size)
  /// learns its size from the definition it is completed against.
  void SetByteSizeFromDefinition(uint64_t byte_size) { SetByteSize(byte_size); }
  void SetExplicitAlignmentInBits(uint64_t align) {
    m_explicit_alignment_in_bits = align;
  }
  void SetIsAnonymousStructOrUnion(bool v) { m_is_anonymous_struct_union = v; }
  void SetAnonymousParent(const RecordType *parent) {
    m_anonymous_parent = parent;
  }
  void SetArgPassingKind(ArgPassingKind kind) { m_arg_passing = kind; }
  void SetMemberFunctionsParsed() { m_member_functions_parsed = true; }
  void AddField(Identifier name, TypeRef type, uint64_t byte_offset,
                uint32_t bitfield_bit_size = 0,
                uint32_t bitfield_bit_offset = 0) {
    m_fields.emplace_back(name, type, byte_offset, bitfield_bit_size,
                          bitfield_bit_offset);
  }
  void AddNestedType(Identifier name, TypeRef type) {
    m_nested_types.emplace_back(name, type);
  }

  // Larger members first so the trailing bit-fields pack into what would
  // otherwise be alignment padding.
  const RecordType *m_anonymous_parent = nullptr;
  std::optional<uint64_t> m_explicit_alignment_in_bits;
  std::vector<Field> m_fields;
  std::vector<std::pair<Identifier, TypeRef>> m_nested_types;
  bool m_complete : 1;
  bool m_is_union : 1;
  bool m_is_class_keyword : 1;
  bool m_is_anonymous_struct_union : 1;
  bool m_member_functions_parsed : 1;
  ArgPassingKind m_arg_passing : 2;
};

/// Common base for "sugar" types that wrap another type and are transparent to
/// most layout/children queries: typedefs and cv-qualified types. Stripping all
/// sugar off a type yields its canonical type (see Type::GetCanonicalType-style
/// desugaring in TypeSystemClike). The transparent virtual queries forward to the
/// underlying type; subclasses override the ones that must differ (e.g. a
/// typedef reports its own name and type class). The concrete sugar kinds live
/// in TypeC.h.
class SugarType : public llvm::RTTIExtends<SugarType, Type> {
public:
  static char ID;

  explicit SugarType(TypeRef underlying_type)
      : m_underlying_type(underlying_type) {}

  /// The immediately-wrapped type. Peel repeatedly to reach the canonical type.
  /// Never null: a `const void`/`typedef void` wraps the `void` builtin, so
  /// sugar always has a concrete underlying type.
  Type *GetUnderlyingType() const { return &m_underlying_type.Get(); }

  // Sugar is see-through: forward the value/layout queries to the wrapped type
  // so a `typedef`/`const` of an aggregate still looks like one.
  Type *Desugar() override { return m_underlying_type.Get().Desugar(); }
  bool IsAggregate() const override {
    return m_underlying_type.Get().IsAggregate();
  }
  bool IsComplete() const override {
    return m_underlying_type.Get().IsComplete();
  }
  // Sugar has the same storage as the type it wraps. Forward dynamically rather
  // than relying on a snapshot taken at creation time: the underlying type's
  // size may only become known later (e.g. a target-dependent builtin size or a
  // lazily completed record), and a stale cached 0 would then be wrong.
  std::optional<uint64_t> GetByteSize() const override {
    return m_underlying_type.Get().GetByteSize();
  }
  lldb::Encoding GetEncoding() const override {
    return m_underlying_type.Get().GetEncoding();
  }
  lldb::Format GetFormat() const override {
    return m_underlying_type.Get().GetFormat();
  }
  lldb::TypeClass GetTypeClass() const override {
    return m_underlying_type.Get().GetTypeClass();
  }
  uint32_t GetTypeInfo() const override {
    return m_underlying_type.Get().GetTypeInfo();
  }
  uint32_t GetNumFields() const override {
    return m_underlying_type.Get().GetNumFields();
  }
  const Field *GetFieldAtIndex(uint32_t idx) const override {
    return m_underlying_type.Get().GetFieldAtIndex(idx);
  }
  uint32_t GetNumBaseClasses() const override {
    return m_underlying_type.Get().GetNumBaseClasses();
  }
  const BaseClass *GetBaseClassAtIndex(uint32_t idx) const override {
    return m_underlying_type.Get().GetBaseClassAtIndex(idx);
  }
  // A `__ptrauth` qualifier can sit behind any amount of see-through sugar.
  std::optional<PtrAuthQualifier> GetPtrAuthQualifier() const override {
    return m_underlying_type.Get().GetPtrAuthQualifier();
  }

private:
  TypeRef m_underlying_type;
};


} // namespace clike
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPE_H
