//===-- Type.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The language-neutral core of the type model: the Type base class, the
// references/structs its API is expressed in, and the mixins and bases shared
// by every concrete type kind. The concrete kinds themselves live in the
// per-language headers next to this one:
//
//   TypeC.h    -- C types (struct, array, pointer, enum, function, sugar, ...)
//   TypeCpp.h  -- C++-only types (class, reference, pointer-to-member, ...)
//   TypeObjC.h -- Objective-C types (@interface and its methods)
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H

#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "Identifier.h"

namespace lldb_private {
namespace cpp_typesystem {

class Context;
class Namespace;
class Type;

// The C++-only members a record can carry. RecordType's API mentions them (so
// a caller holding a `RecordType *` can ask for them generically) but only
// ClassType stores any, so their definitions live in TypeCpp.h.
struct TemplateArgument;
struct MemberFunction;
struct StaticDataMember;

/// References a type, potentially in another Context. Type nodes are owned by
/// their Context, so a bare `Type *` does not by itself say where a referenced
/// type lives; a TypeRef pairs the type pointer with its owning Context. Every
/// type class stores TypeRefs (never a bare `Type *`) to reference other types,
/// because a type may reference another type in a different Context.
class TypeRef {
public:
  TypeRef() = default;
  TypeRef(Context &context, Type *type) : m_context(&context), m_type(type) {}

  /// The referenced type, or null for an empty reference (e.g. the pointee of
  /// a `void *`).
  Type *Get() const { return m_type; }
  /// The Context that owns the referenced type, or null for an empty reference.
  Context *GetContext() const { return m_context; }

  /// True when this refers to a type (false for an empty reference).
  explicit operator bool() const { return m_type != nullptr; }

private:
  Context *m_context = nullptr;
  Type *m_type = nullptr;
};

static_assert(sizeof(TypeRef) <= sizeof(void *) * 2,
              "TypeRef is expected to be a small reference class!");

/// A single data member of a record type.
struct Field {
  Identifier name;
  /// The type of this member.
  TypeRef type;
  /// Offset of this member (or, for a bitfield, of its storage unit) from the
  /// start of the record, in bytes.
  uint64_t byte_offset = 0;
  /// For a bitfield: its width in bits. Zero means this is not a bitfield.
  uint32_t bitfield_bit_size = 0;
  /// For a bitfield: the offset of its first bit within the storage unit at
  /// byte_offset.
  uint32_t bitfield_bit_offset = 0;

  bool IsBitfield() const { return bitfield_bit_size != 0; }
};

/// A direct base class of a C++ class type. Objective-C reuses this to model an
/// interface's superclass (see ObjCInterfaceType), which is why it lives here
/// rather than in TypeCpp.h.
struct BaseClass {
  /// The base class type.
  TypeRef type;
  /// Offset of the base class subobject from the start of the derived record,
  /// in bytes. For a virtual base this is not a reliable constant (DWARF encodes
  /// it as a location expression evaluated against a live object), so it is left
  /// 0 and consumers must derive the real offset from a live object instead
  /// (see vbase_offset_offset).
  uint64_t byte_offset = 0;
  /// True if this is a virtual base class (`class D : virtual B`).
  bool is_virtual = false;
  /// For a virtual base under the Itanium ABI: the (positive) byte offset that,
  /// subtracted from the derived object's vtable pointer, yields the address of
  /// the slot holding this virtual base's offset. That is, the base subobject
  /// lives at `obj + *(int*)(*(void**)obj - vbase_offset_offset)`. Extracted
  /// from the base's DWARF DW_AT_data_member_location expression (the standard
  /// `DW_OP_dup DW_OP_deref DW_OP_constu N DW_OP_minus DW_OP_deref DW_OP_plus`
  /// form). std::nullopt when not a virtual base or the expression didn't match,
  /// in which case consumers fall back to byte_offset.
  std::optional<uint64_t> vbase_offset_offset;
};

/// Represents everything needed to understand a type.
///
/// A pointer to a Type is what TypeSystemCpp hands out as its
/// lldb::opaque_compiler_type_t, so the virtual functions below are the queries
/// that back the CompilerType API.
///
/// Only a named type (a record, typedef or enum -- see the GetName() family
/// below) carries a spelling/namespace/alignment of its own; a "structural"
/// type (pointer, reference, array, function, sugar, ...) has none and is far
/// more numerous in a typical program (e.g. every pointer/reference/const
/// qualification creates a distinct instance). So that storage is NOT held
/// here where every Type would pay for it -- it lives only on the handful of
/// classes that use it (RecordType, EnumType, TypedefType, BuiltinType), and
/// is reached generically through these virtual accessors (defaulting to
/// "no name" for a structural type).
class Type : public llvm::RTTIExtends<Type, llvm::RTTIRoot> {
public:
  /// LLVM-style RTTI support (isa<>/cast<>/dyn_cast<>). Because Type already
  /// has a vtable, RTTIExtends implements this via a virtual dispatch keyed on
  /// the per-class `ID` address, so no per-object discriminator is needed.
  static char ID;

  virtual ~Type() = default;

  /// The type's own name. For a record this is the fully-qualified,
  /// template-argument-bearing DWARF spelling (used to recover an enclosing
  /// *class* scope that GetDeclContext() can't represent -- see
  /// AppendClassScopePrefix); for a builtin/typedef/enum it is the plain
  /// spelling. Empty for a structural type, which has no name of its own.
  virtual Identifier GetName() const { return Identifier(); }

  /// The namespace this type is declared in (null for the global namespace,
  /// and for a structural type, which has no declaration context). Used to
  /// build the qualified display name while skipping inline namespaces. See
  /// cpp_typesystem::Namespace.
  virtual const Namespace *GetDeclContext() const { return nullptr; }
  /// Only ever invoked (via Builder::SetDeclContext, generically through a
  /// `Type *`) on a type that overrides GetDeclContext() with real storage.
  virtual void SetDeclContext(const Namespace *ns) {
    llvm_unreachable("this Type kind has no declaration context to set");
  }

  /// The unqualified spelling as written in the debug info (e.g. `string` or
  /// `vector<int, std::allocator<int>>`), without any namespace qualification.
  /// Used, together with GetDeclContext() and the template arguments, to build
  /// the (possibly simplified) display name. Empty for a structural type.
  virtual Identifier GetUnqualifiedName() const { return Identifier(); }
  /// Only ever invoked (via Builder::SetUnqualifiedName, generically through a
  /// `Type *`) on a type that overrides GetUnqualifiedName() with real storage.
  virtual void SetUnqualifiedName(Identifier name) {
    llvm_unreachable("this Type kind has no unqualified name to set");
  }

  /// The size of a value of this type, in bytes, or std::nullopt when unknown
  /// (e.g. an incomplete record, or a function type). Most structural types
  /// (pointers, references, arrays, sugar, ...) either derive their size from
  /// another type or share the target's fixed pointer size, so the storage for
  /// an explicit size is NOT held here -- where every Type would pay 8 bytes
  /// for it -- but only on the kinds that need it (see ByteSizedType and the
  /// pointer/array/complex overrides in the per-language headers).
  virtual std::optional<uint64_t> GetByteSize() const { return std::nullopt; }

  /// An explicitly-specified alignment in bits (from `DW_AT_alignment`, e.g.
  /// `alignas(128)`), if the debug info recorded one. When absent, callers fall
  /// back to deriving a natural alignment from the size. Only a record can
  /// carry one; every other kind reports "none".
  virtual std::optional<uint64_t> GetAlignInBits() const { return std::nullopt; }

  /// This type's alignment in bits: the explicitly-recorded one when the debug
  /// info had it (see GetAlignInBits), else one derived from the size, or
  /// std::nullopt when neither is available.
  ///
  /// The model doesn't record alignment for most types. For a scalar the
  /// derived alignment is its size; for an aggregate it is the largest power of
  /// two that divides the size (the natural alignment of the standard-layout
  /// types debug info produces), capped at 8 bytes -- the fundamental alignment
  /// on the supported targets. A pointer/reference overrides this with the
  /// target's pointer alignment.
  virtual std::optional<uint64_t> GetAlignmentInBits() const;

  /// Peel "sugar" (typedef/cv-qualifier/elaborated/ptrauth) off this type to
  /// reach its canonical type. Mirrors clang's RemoveWrappingTypes: queries
  /// about layout and children should see through aliases and qualifiers, so
  /// most of TypeSystemCpp's query methods call this first. Sugar overrides it
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
};

/// Null-tolerant wrapper around Type::Desugar(), for the many call sites that
/// desugar a type that may not exist (an absent pointee, an unset TypeRef).
inline Type *Desugar(Type *t) { return t ? t->Desugar() : nullptr; }
inline const Type *Desugar(const Type *t) { return t ? t->Desugar() : nullptr; }

/// Mixin adding the name/declaration-context storage that Type declares (as
/// virtual accessors defaulting to "no name") but does not itself store --
/// see the comment on Type for why. Inherited by every named type (a record,
/// typedef or enum) over whichever base it would otherwise have (`Type`
/// directly, or `SugarType` for a typedef), so the storage and its accessor
/// overrides are written once instead of copy-pasted per class.
template <typename Base> class NamedType : public Base {
public:
  Identifier GetName() const override { return m_name; }
  void SetName(Identifier name) { m_name = name; }
  const Namespace *GetDeclContext() const override { return m_decl_context; }
  void SetDeclContext(const Namespace *ns) override { m_decl_context = ns; }
  Identifier GetUnqualifiedName() const override { return m_unqualified_name; }
  void SetUnqualifiedName(Identifier name) override {
    m_unqualified_name = name;
  }

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
  std::optional<uint64_t> GetByteSize() const override {
    return m_byte_size == kNoByteSize ? std::nullopt
                                      : std::optional<uint64_t>(m_byte_size);
  }
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
class RecordType : public llvm::RTTIExtends<RecordType, NamedType<ByteSizedType<Type>>> {
public:
  static char ID;

  // The flag members below are packed bit-fields with no default member
  // initializers (those are a C++20 feature; LLVM builds as C++17), so they
  // are given their defaults here.
  RecordType()
      : m_complete(false), m_is_union(false), m_is_class_keyword(false),
        m_is_anonymous_struct_union(false), m_member_functions_parsed(false),
        m_arg_passing(ArgPassingKind::Unspecified) {}

  bool IsAggregate() const override { return true; }
  bool IsComplete() const override { return m_complete; }

  /// An explicitly-specified alignment (see Type::GetAlignInBits); only a
  /// record can carry one, so unlike name/decl-context this isn't shared via
  /// NamedType.
  std::optional<uint64_t> GetAlignInBits() const override {
    return m_align_in_bits;
  }
  void SetAlignInBits(std::optional<uint64_t> align) {
    m_align_in_bits = align;
  }

  /// True if this record is a `union` (all members share offset 0). Needed so
  /// the type can be reconstructed with the correct tag kind.
  bool IsUnion() const { return m_is_union; }

  /// True if this record was declared with the `class` keyword (as opposed to
  /// `struct`). In a C++ translation unit both are modeled as a ClassType (the
  /// struct/class keyword does not change layout), so this preserves the source
  /// tag keyword purely for naming an *unnamed* record ("(unnamed class)" vs
  /// "(unnamed struct)"), matching clang's TagDecl printing.
  bool IsClassKeyword() const { return m_is_class_keyword; }

  /// True if this record is an anonymous struct/union: an *unnamed* record that
  /// is embedded as an *unnamed* member of its enclosing record, so its members
  /// are injected into the enclosing scope (e.g. `struct { int y; };`). This is
  /// distinct from a merely unnamed record that is given a member name
  /// (`struct { int x; } named;`), which is not anonymous. Mirrors clang's
  /// RecordDecl::isAnonymousStructOrUnion(); set by the DWARF parser.
  bool IsAnonymousStructOrUnion() const { return m_is_anonymous_struct_union; }

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

  /// True if this record is a class-template instantiation, regardless of how
  /// many template arguments it has. This is distinct from
  /// `GetNumTemplateArguments() > 0`: a specialization of a variadic template
  /// over an *empty* pack (e.g. `TypePack<>`) has zero arguments yet is still a
  /// template instantiation and must print an (empty) `<>` argument list.
  ///
  /// Template arguments, member functions and static data members are C++-only
  /// (a plain C struct/union or an Objective-C interface never has them), so
  /// their storage lives on ClassType (see TypeCpp.h), not here; these
  /// accessors default to "none" and are overridden by ClassType.
  virtual bool IsTemplateInstantiation() const { return false; }

  /// Template arguments, if this record is a class-template instantiation.
  virtual uint32_t GetNumTemplateArguments() const { return 0; }
  virtual const TemplateArgument *GetTemplateArgumentAtIndex(uint32_t idx) const {
    return nullptr;
  }

  /// Member functions of this record (used to resolve/call methods from an
  /// expression).
  virtual uint32_t GetNumMemberFunctions() const { return 0; }
  virtual const MemberFunction *GetMemberFunctionAtIndex(uint32_t idx) const {
    return nullptr;
  }

  /// Static data members of this record (`static int s;`). Used by the
  /// expression evaluator to resolve `record.s` / `Record::s` and, for a
  /// constant integral member, to fold its value.
  virtual uint32_t GetNumStaticDataMembers() const { return 0; }
  virtual const StaticDataMember *GetStaticDataMemberAtIndex(uint32_t idx) const {
    return nullptr;
  }

  /// True once this record's member functions have been parsed. Member
  /// functions are only needed by the expression evaluator (to call methods),
  /// so -- unlike fields and base classes -- they are parsed lazily, in a
  /// separate step after the record is otherwise complete. This avoids pulling
  /// in every method's signature (and the types it references) merely to
  /// inspect a value of this type. A complete record may still have unparsed
  /// member functions.
  bool AreMemberFunctionsParsed() const { return m_member_functions_parsed; }

  /// Look up a type declared directly inside this record (a nested typedef,
  /// class, union or enum) by its unqualified name. Returns null if there is no
  /// such nested type. Data formatters use this to reach a container's internal
  /// helper types (e.g. a tree's "__node_pointer").
  Type *GetNestedTypeWithName(llvm::StringRef name) const {
    for (const auto &entry : m_nested_types)
      if (entry.first.GetName() == name)
        return entry.second.Get();
    return nullptr;
  }

  /// Number of types declared directly inside this record (nested typedefs,
  /// classes, unions or enums). Used by the expression evaluator to decide
  /// whether a generated record needs a name-lookup callback for
  /// `Record::Nested`; the types themselves are looked up by name via
  /// GetNestedTypeWithName.
  uint32_t GetNumNestedTypes() const { return m_nested_types.size(); }

  /// Whether \p t (a possibly-sugared record) has any data members, considering
  /// base classes recursively. A vtable pointer is not a field, so a
  /// polymorphic-but-otherwise-empty class answers false. Mirrors
  /// TypeSystemClang::RecordHasFields; this drives whether an empty base class
  /// is omitted from a value's children (the `omit_empty_base_classes` flag
  /// threaded through the child-index queries).
  ///
  /// \p complete is invoked on each (lazily-parsed) record before it is
  /// inspected, so its field count is known. Completion isn't something the
  /// type model can do on its own -- it needs the SymbolFile -- so the caller
  /// supplies it.
  static bool HasFields(Type *t,
                        llvm::function_ref<void(Type *)> complete);

  /// If this record is a Homogeneous Floating-point/Vector Aggregate, the
  /// element type every one of its fields has; null otherwise. \p num_fields
  /// receives the field count on success (0 otherwise).
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
  Type *GetHomogeneousAggregateBase(uint32_t &num_fields) const;

private:
  // Structural mutation happens after creation (during lazy completion), so it
  // is gated: only Context can perform it, and Context is only reachable
  // through a cpp_typesystem::Builder.
  friend class Context;
  void SetIsComplete(bool complete) { m_complete = complete; }
  void SetIsAnonymousStructOrUnion(bool v) { m_is_anonymous_struct_union = v; }
  void SetAnonymousParent(const RecordType *parent) {
    m_anonymous_parent = parent;
  }
  void SetArgPassingKind(ArgPassingKind kind) { m_arg_passing = kind; }
  void SetMemberFunctionsParsed() { m_member_functions_parsed = true; }
  void AddField(Identifier name, TypeRef type, uint64_t byte_offset,
                uint32_t bitfield_bit_size = 0,
                uint32_t bitfield_bit_offset = 0) {
    Field f;
    f.name = name;
    f.type = type;
    f.byte_offset = byte_offset;
    f.bitfield_bit_size = bitfield_bit_size;
    f.bitfield_bit_offset = bitfield_bit_offset;
    m_fields.push_back(f);
  }
  void AddNestedType(Identifier name, TypeRef type) {
    m_nested_types.emplace_back(name, type);
  }

  // Larger members first so the trailing bit-fields pack into what would
  // otherwise be alignment padding.
  const RecordType *m_anonymous_parent = nullptr;
  std::optional<uint64_t> m_align_in_bits;
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
/// desugaring in TypeSystemCpp). The transparent virtual queries forward to the
/// underlying type; subclasses override the ones that must differ (e.g. a
/// typedef reports its own name and type class). The concrete sugar kinds live
/// in TypeC.h.
class SugarType : public llvm::RTTIExtends<SugarType, Type> {
public:
  static char ID;

  /// The immediately-wrapped type. Peel repeatedly to reach the canonical type.
  /// Never null: a `const void`/`typedef void` wraps the `void` builtin, so
  /// sugar always has a concrete underlying type (enforced by the Context
  /// factories that create these).
  Type *GetUnderlyingType() const { return m_underlying_type.Get(); }
  void SetUnderlyingType(TypeRef type) {
    assert(type && "sugar must wrap a type (use the void builtin for void)");
    m_underlying_type = type;
  }

  // Sugar is see-through: forward the value/layout queries to the wrapped type
  // so a `typedef`/`const` of an aggregate still looks like one.
  Type *Desugar() override { return m_underlying_type.Get()->Desugar(); }
  bool IsAggregate() const override {
    return m_underlying_type.Get()->IsAggregate();
  }
  bool IsComplete() const override {
    return m_underlying_type.Get()->IsComplete();
  }
  // Sugar has the same storage as the type it wraps. Forward dynamically rather
  // than relying on a snapshot taken at creation time: the underlying type's
  // size may only become known later (e.g. a target-dependent builtin size or a
  // lazily completed record), and a stale cached 0 would then be wrong.
  std::optional<uint64_t> GetByteSize() const override {
    return m_underlying_type.Get()->GetByteSize();
  }
  lldb::Encoding GetEncoding() const override {
    return m_underlying_type.Get()->GetEncoding();
  }
  lldb::Format GetFormat() const override {
    return m_underlying_type.Get()->GetFormat();
  }
  lldb::TypeClass GetTypeClass() const override {
    return m_underlying_type.Get()->GetTypeClass();
  }
  uint32_t GetTypeInfo() const override {
    return m_underlying_type.Get()->GetTypeInfo();
  }
  uint32_t GetNumFields() const override {
    return m_underlying_type.Get()->GetNumFields();
  }
  const Field *GetFieldAtIndex(uint32_t idx) const override {
    return m_underlying_type.Get()->GetFieldAtIndex(idx);
  }
  uint32_t GetNumBaseClasses() const override {
    return m_underlying_type.Get()->GetNumBaseClasses();
  }
  const BaseClass *GetBaseClassAtIndex(uint32_t idx) const override {
    return m_underlying_type.Get()->GetBaseClassAtIndex(idx);
  }

private:
  TypeRef m_underlying_type;
};

} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
