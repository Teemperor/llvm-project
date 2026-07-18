//===-- Type.h --------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H

#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "Identifier.h"

namespace lldb_private {
namespace cpp_typesystem {

class Context;
class Namespace;
class Type;

/// References a type, potentially in another Context. Type nodes are owned by
/// their Context, so a bare `Type *` does not by itself say where a referenced
/// type lives; a TypeRef pairs the type pointer with its owning Context. Every
/// class in this file stores TypeRefs (never a bare `Type *`) to reference
/// other types, because a type may reference another type in a different
/// Context.
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

/// A static data member of a record type (`static int s;`). Unlike a Field it
/// occupies no storage inside the object; at runtime it is a global whose
/// address is resolved through its mangled/linkage name. An integral
/// `static const`/`constexpr` member may instead (or additionally) carry a
/// compile-time constant value and have no storage at all.
struct StaticDataMember {
  Identifier name;
  /// The type of this member.
  TypeRef type;
  /// The mangled (linkage) name of the member's definition, used to resolve
  /// its runtime address from an expression. Empty when the member has no
  /// storage (a constant-only `static const`/`constexpr` member).
  Identifier mangled_name;
  /// The compile-time constant value for an integral `static const`/`constexpr`
  /// member (DWARF's DW_AT_const_value), as raw bits; std::nullopt when the
  /// member has no such constant.
  std::optional<uint64_t> const_value;

  bool HasConstValue() const { return const_value.has_value(); }
};

/// A direct base class of a C++ class type.
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

/// A template argument of a class template instantiation (e.g. the `int` and
/// `std::allocator<int>` of `std::vector<int, std::allocator<int>>`). Data
/// formatters rely on these to recover, for instance, a container's element
/// type.
struct TemplateArgument {
  lldb::TemplateArgumentKind kind = lldb::eTemplateArgumentKindNull;
  /// Type argument: the argument's type. Integral argument: the value's type.
  TypeRef type;
  /// Integral argument: the raw value bits (interpret using `type`'s
  /// signedness/size).
  uint64_t integral_value = 0;
  /// Template argument (a template-template parameter, e.g. the `T1` in
  /// `C<float, T1>`): the referenced template's name. Such arguments are not a
  /// modeled type, so only their spelling is kept -- enough to reconstruct the
  /// instantiation's display name.
  Identifier name;
  /// True when this argument was defaulted (DWARF's DW_AT_default_value), so it
  /// is hidden when building the type's display name (`std::vector<int>` rather
  /// than `std::vector<int, std::allocator<int>>`).
  bool is_default = false;
};

/// The ref-qualifier applied to a member function's implicit object parameter
/// (the `&`/`&&` in `void f() &` / `void f() &&`). A class can overload on this,
/// so it must be modeled to disambiguate calls.
enum class RefQualifier {
  None,   ///< No ref-qualifier (`void f()`).
  LValue, ///< Lvalue ref-qualifier (`void f() &`).
  RValue, ///< Rvalue ref-qualifier (`void f() &&`).
};

/// Whether a member function is an ordinary method or a special member the
/// expression evaluator must build with a proper C++ declaration name (a
/// constructor/destructor uses a getCXXConstructorName/getCXXDestructorName,
/// not an identifier). Detected from DWARF (see
/// DWARFASTParserCpp::CompleteMemberFunctionsFromDWARF).
enum class MemberFunctionKind {
  Method,      ///< An ordinary (possibly static) member function.
  Constructor, ///< A constructor (`Foo(...)`).
  Destructor,  ///< A destructor (`~Foo()`).
};

/// A member function of a record type. Only what an expression needs to build a
/// clang::CXXMethodDecl and call it: the (non-`this`) signature, the mangled
/// name (so the JIT can resolve the callee), and the basic C++ flags.
struct MemberFunction {
  Identifier name;
  /// The function's type (a FunctionType); its parameters exclude the implicit
  /// object parameter for non-static methods.
  TypeRef type;
  /// The asm label (an lldb FunctionCallLabel) the call resolves through, so
  /// the JIT can find the callee in the inferior.
  Identifier asm_label;
  /// The raw mangled (linkage) name, if the debug info recorded one. Used by
  /// SBTypeMemberFunction::GetMangledName/GetDemangledName (distinct from
  /// asm_label, which is a FunctionCallLabel wrapping this name plus location).
  Identifier mangled_name;
  bool is_static = false;
  bool is_const = false;
  bool is_volatile = false;
  bool is_virtual = false;
  /// The `&`/`&&` ref-qualifier, if any (overloadable, so must be modeled).
  RefQualifier ref_qualifier = RefQualifier::None;
  /// Whether this is an ordinary method, a constructor or a destructor. A
  /// constructor/destructor must be built with the proper C++ declaration name
  /// so clang recognizes `Foo(2)` as a constructor call.
  MemberFunctionKind kind = MemberFunctionKind::Method;
};

/// An Objective-C method of an interface. Only what an expression needs to
/// build a clang::ObjCMethodDecl and message-send it: the full method name
/// (`-[Class sel:]` / `+[Class sel:]`, which encodes both the selector and
/// whether it is an instance or class method), the (self/_cmd-stripped)
/// signature, and the asm label so the JIT can resolve the callee. Whether the
/// method is variadic matters for the message-send lowering.
struct ObjCMethod {
  /// The full name as written in the debug info, e.g. `+[Foo doThing:with:]`.
  Identifier name;
  /// The method's type (a FunctionType); its parameters exclude the implicit
  /// `self`/`_cmd` parameters.
  TypeRef type;
  /// The asm label (an lldb FunctionCallLabel) the call resolves through.
  Identifier asm_label;
  /// True for a class method (`+[...]`), false for an instance method.
  bool is_class_method = false;
  bool is_variadic = false;
  /// True for an `objc_direct` method: the compiler resolved every call
  /// statically to this implementation rather than going through
  /// objc_msgSend. The synthesized clang::ObjCMethodDecl must carry an
  /// ObjCDirectAttr so the expression parser's message-send codegen emits a
  /// direct call instead of a dynamic dispatch (which would send an
  /// unrecognized selector, since a direct method is never registered with
  /// the ObjC runtime).
  bool is_direct = false;
};

/// Represents everything needed to understand a type.
///
/// A pointer to a Type is what TypeSystemCpp hands out as its
/// lldb::opaque_compiler_type_t, so the virtual functions below are the queries
/// that back the CompilerType API.
class Type : public llvm::RTTIExtends<Type, llvm::RTTIRoot> {
public:
  /// LLVM-style RTTI support (isa<>/cast<>/dyn_cast<>). Because Type already
  /// has a vtable, RTTIExtends implements this via a virtual dispatch keyed on
  /// the per-class `ID` address, so no per-object discriminator is needed.
  static char ID;

  virtual ~Type() = default;

  Identifier GetName() const { return m_name; }
  void SetName(Identifier name) { m_name = name; }

  /// The namespace this type is declared in (null for the global namespace).
  /// Used to build the qualified display name while skipping inline
  /// namespaces. See cpp_typesystem::Namespace.
  const Namespace *GetDeclContext() const { return m_decl_context; }
  void SetDeclContext(const Namespace *ns) { m_decl_context = ns; }

  /// The unqualified spelling as written in the debug info (e.g. `string` or
  /// `vector<int, std::allocator<int>>`), without any namespace qualification.
  /// Used, together with GetDeclContext() and the template arguments, to build
  /// the (possibly simplified) display name.
  Identifier GetUnqualifiedName() const { return m_unqualified_name; }
  void SetUnqualifiedName(Identifier name) { m_unqualified_name = name; }

  virtual std::optional<uint64_t> GetByteSize() const { return m_byte_size; }
  void SetByteSize(std::optional<uint64_t> byte_size) { m_byte_size = byte_size; }

  /// An explicitly-specified alignment in bits (from `DW_AT_alignment`, e.g.
  /// `alignas(128)`), if the debug info recorded one. When absent, callers fall
  /// back to deriving a natural alignment from the size.
  std::optional<uint64_t> GetAlignInBits() const { return m_align_in_bits; }
  void SetAlignInBits(std::optional<uint64_t> align) { m_align_in_bits = align; }

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

private:
  Identifier m_name;
  Identifier m_unqualified_name;
  const Namespace *m_decl_context = nullptr;
  std::optional<uint64_t> m_byte_size;
  std::optional<uint64_t> m_align_in_bits;
};

/// Common base for C/C++ record types (struct/class/union). Owns the data
/// members and the forward-declaration/completion state that every record
/// shares. C++-only information (such as base classes) lives on ClassType, so
/// a plain StructType never reserves storage for it.
class RecordType : public llvm::RTTIExtends<RecordType, Type> {
public:
  static char ID;

  bool IsAggregate() const override { return true; }
  bool IsComplete() const override { return m_complete; }

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
  bool IsTemplateInstantiation() const { return m_is_template; }

  /// Template arguments, if this record is a class-template instantiation.
  uint32_t GetNumTemplateArguments() const { return m_template_args.size(); }
  const TemplateArgument *GetTemplateArgumentAtIndex(uint32_t idx) const {
    if (idx < m_template_args.size())
      return &m_template_args[idx];
    return nullptr;
  }

  /// Member functions of this record (used to resolve/call methods from an
  /// expression).
  uint32_t GetNumMemberFunctions() const { return m_methods.size(); }
  const MemberFunction *GetMemberFunctionAtIndex(uint32_t idx) const {
    if (idx < m_methods.size())
      return &m_methods[idx];
    return nullptr;
  }

  /// Static data members of this record (`static int s;`). Used by the
  /// expression evaluator to resolve `record.s` / `Record::s` and, for a
  /// constant integral member, to fold its value.
  uint32_t GetNumStaticDataMembers() const {
    return m_static_data_members.size();
  }
  const StaticDataMember *GetStaticDataMemberAtIndex(uint32_t idx) const {
    if (idx < m_static_data_members.size())
      return &m_static_data_members[idx];
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

private:
  // Structural mutation happens after creation (during lazy completion, which
  // may run on worker threads), so it is gated: only Context can perform it,
  // and Context is only reachable through TypeSystemCpp's locked Builder.
  friend class Context;
  void SetIsComplete(bool complete) { m_complete = complete; }
  void SetIsTemplateInstantiation(bool is_template) {
    m_is_template = is_template;
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
    Field f;
    f.name = name;
    f.type = type;
    f.byte_offset = byte_offset;
    f.bitfield_bit_size = bitfield_bit_size;
    f.bitfield_bit_offset = bitfield_bit_offset;
    m_fields.push_back(f);
  }
  void AddTemplateArgument(TemplateArgument arg) {
    m_template_args.push_back(arg);
  }
  void AddNestedType(Identifier name, TypeRef type) {
    m_nested_types.emplace_back(name, type);
  }
  void AddMemberFunction(MemberFunction method) {
    m_methods.push_back(method);
  }
  void AddStaticDataMember(StaticDataMember member) {
    m_static_data_members.push_back(member);
  }

  bool m_complete = false;
  bool m_is_union = false;
  bool m_is_class_keyword = false;
  bool m_is_template = false;
  bool m_is_anonymous_struct_union = false;
  const RecordType *m_anonymous_parent = nullptr;
  ArgPassingKind m_arg_passing = ArgPassingKind::Unspecified;
  bool m_member_functions_parsed = false;
  std::vector<Field> m_fields;
  std::vector<TemplateArgument> m_template_args;
  std::vector<std::pair<Identifier, TypeRef>> m_nested_types;
  std::vector<MemberFunction> m_methods;
  std::vector<StaticDataMember> m_static_data_members;
};

/// A C struct/union type. Records parsed from a C++ translation unit are
/// backed by ClassType instead, since only those can have base classes.
class StructType : public llvm::RTTIExtends<StructType, RecordType> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassStruct;
  }
};

/// A C++ class type. In addition to the data members every record has, it can
/// carry direct base classes.
class ClassType : public llvm::RTTIExtends<ClassType, RecordType> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassClass;
  }

  uint32_t GetNumBaseClasses() const override { return m_bases.size(); }
  const BaseClass *GetBaseClassAtIndex(uint32_t idx) const override {
    if (idx < m_bases.size())
      return &m_bases[idx];
    return nullptr;
  }

  bool IsPolymorphic() const override { return m_is_polymorphic; }

private:
  // Gated like RecordType's mutators (see there): only Context, reached through
  // the locked Builder, may add base classes.
  friend class Context;
  void AddBaseClass(TypeRef type, uint64_t byte_offset,
                    bool is_virtual = false,
                    std::optional<uint64_t> vbase_offset_offset = std::nullopt) {
    m_bases.push_back(
        BaseClass{type, byte_offset, is_virtual, vbase_offset_offset});
  }
  void SetPolymorphic() { m_is_polymorphic = true; }

  std::vector<BaseClass> m_bases;
  bool m_is_polymorphic = false;
};

/// An Objective-C class type (`@interface Foo`). Its instance variables
/// (ivars) are modeled as record fields and its (single) superclass as its one
/// base class, so it reuses the RecordType field/base-class machinery.
///
/// Unlike a C/C++ record, an ObjC ivar's byte offset is NOT reliably present in
/// DWARF: the compiler emits 0 for every ivar (see the identical
/// DW_AT_data_member_location values in the debug info). The authoritative
/// offset lives in the Objective-C runtime -- specifically in the
/// `OBJC_IVAR_$_Class.ivar` offset symbols -- and is resolved against a live
/// process at value-inspection time (see
/// TypeSystemCpp::GetChildCompilerTypeAtIndex). The field byte_offset stored
/// here is therefore only a fallback.
class ObjCInterfaceType
    : public llvm::RTTIExtends<ObjCInterfaceType, RecordType> {
public:
  static char ID;

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassObjCObject;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsObjC | lldb::eTypeIsStructUnion;
  }

  // The superclass is exposed as this interface's single base class.
  uint32_t GetNumBaseClasses() const override { return m_superclass ? 1 : 0; }
  const BaseClass *GetBaseClassAtIndex(uint32_t idx) const override {
    if (idx == 0 && m_superclass)
      return &*m_superclass;
    return nullptr;
  }

  /// Objective-C methods of this interface (used to build ObjCMethodDecls so
  /// the expression parser can type-check and lower a message send). Parsed
  /// lazily alongside member functions (see AreMemberFunctionsParsed).
  uint32_t GetNumObjCMethods() const { return m_objc_methods.size(); }
  const ObjCMethod *GetObjCMethodAtIndex(uint32_t idx) const {
    if (idx < m_objc_methods.size())
      return &m_objc_methods[idx];
    return nullptr;
  }

private:
  // Gated like RecordType's mutators (see there): only Context, reached through
  // the locked Builder, may set the superclass.
  friend class Context;
  void SetSuperClass(TypeRef type) {
    m_superclass = BaseClass{type, /*byte_offset=*/0, /*is_virtual=*/false,
                             /*vbase_offset_offset=*/std::nullopt};
  }
  void AddObjCMethod(ObjCMethod method) {
    m_objc_methods.push_back(std::move(method));
  }

  std::optional<BaseClass> m_superclass;
  std::vector<ObjCMethod> m_objc_methods;
};

/// A C array type: a fixed number of contiguous elements of the same type.
class ArrayType : public llvm::RTTIExtends<ArrayType, Type> {
public:
  static char ID;

  Type *GetElementType() const { return m_element_type.Get(); }
  void SetElementType(TypeRef type) { m_element_type = type; }

  /// Number of elements, or std::nullopt for an array of unknown bound.
  std::optional<uint64_t> GetNumElements() const { return m_num_elements; }
  void SetNumElements(std::optional<uint64_t> num_elements) {
    m_num_elements = num_elements;
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
  lldb::TypeClass GetTypeClass() const override {
    return m_is_vector ? lldb::eTypeClassVector : lldb::eTypeClassArray;
  }
  uint32_t GetTypeInfo() const override {
    if (m_is_vector)
      return lldb::eTypeHasChildren | lldb::eTypeIsVector;
    return lldb::eTypeHasChildren | lldb::eTypeIsArray;
  }

private:
  TypeRef m_element_type;
  std::optional<uint64_t> m_num_elements;
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

  /// True if this models an Apple "blocks" pointer (`int (^)(int)`), i.e. the
  /// pointee is a FunctionType but the runtime representation and calling
  /// convention are those of a block, not a plain function pointer. This is
  /// how DWARF's `DW_AT_APPLE_block` closures are represented.
  bool IsBlockPointer() const { return m_is_block; }
  void SetIsBlockPointer(bool is_block) { m_is_block = is_block; }

  lldb::Encoding GetEncoding() const override { return lldb::eEncodingUint; }
  lldb::Format GetFormat() const override { return lldb::eFormatHex; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassPointer;
  }
  uint32_t GetTypeInfo() const override {
    uint32_t info =
        lldb::eTypeHasChildren | lldb::eTypeIsPointer | lldb::eTypeHasValue;
    // A pointer to an Objective-C interface (`Foo *` / `id`) is itself an
    // Objective-C construct: report eTypeIsObjC so that the value printer's
    // ObjC pointer-expansion (dwim-print's SetExpandPointerTypeFlags) and the
    // ObjC language runtime treat it as an object pointer. (Sugar between the
    // pointer and the interface is peeled by the caller via Desugar; the
    // pointee stored here is normally the interface directly.)
    const Type *pointee = m_pointee_type.Get();
    if (pointee && llvm::isa<ObjCInterfaceType>(pointee))
      info |= lldb::eTypeIsObjC;
    // `id` / `Class` are modeled as a pointer to the opaque `objc_object` /
    // `objc_class` record (see TypeSystemCpp::IsPossibleDynamicType /
    // GetMinimumLanguage); recognize that idiom too, so e.g.
    // ObjCLanguage::IsNilReference still prints a null `id`/`Class` as "nil"
    // instead of "0x0".
    if (auto *rec = llvm::dyn_cast_or_null<RecordType>(pointee)) {
      llvm::StringRef name = rec->GetName().GetName();
      if (name == "objc_object" || name == "objc_class")
        info |= lldb::eTypeIsObjC;
    }
    return info;
  }

private:
  TypeRef m_pointee_type;
  bool m_is_block = false;
};

/// A C++ reference type: lvalue `T &` or rvalue `T &&`. At runtime a reference
/// is represented by an address (like a pointer), but it is transparent when
/// exploring its value: its single child is the referenced object.
class ReferenceType : public llvm::RTTIExtends<ReferenceType, Type> {
public:
  static char ID;

  /// The type this reference refers to. E.g., for `int &` this is `int`.
  Type *GetPointeeType() const { return m_pointee_type.Get(); }
  void SetPointeeType(TypeRef type) { m_pointee_type = type; }

  /// True for an rvalue reference (`T &&`), false for an lvalue one (`T &`).
  bool IsRValue() const { return m_is_rvalue; }
  void SetIsRValue(bool is_rvalue) { m_is_rvalue = is_rvalue; }

  lldb::Encoding GetEncoding() const override { return lldb::eEncodingUint; }
  lldb::Format GetFormat() const override { return lldb::eFormatHex; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassReference;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeHasChildren | lldb::eTypeIsReference | lldb::eTypeHasValue;
  }

private:
  TypeRef m_pointee_type;
  bool m_is_rvalue = false;
};

/// A C++ pointer-to-member type: a pointer to a non-static data member
/// (`T C::*`) or non-static member function (`R (C::*)(Args...)`) of class
/// `C`. Unlike PointerType/ReferenceType this is not transparent -- it has no
/// children and cannot be dereferenced without an object of the containing
/// class -- so it behaves like an opaque scalar value for layout purposes.
/// Its byte size is ABI-defined (Itanium: sizeof(ptrdiff_t) for a data member,
/// two pointers for a member function) and is computed by the DWARF parser
/// (DW_AT_byte_size is not reliably emitted for this DIE), not derived here.
class MemberPointerType
    : public llvm::RTTIExtends<MemberPointerType, Type> {
public:
  static char ID;

  /// The type of the pointed-to member: the data member's type, or the member
  /// function's type (a FunctionType).
  Type *GetPointeeType() const { return m_pointee_type.Get(); }
  void SetPointeeType(TypeRef type) { m_pointee_type = type; }

  /// The class this is a pointer-to-member of.
  Type *GetContainingType() const { return m_containing_type.Get(); }
  void SetContainingType(TypeRef type) { m_containing_type = type; }

  lldb::Encoding GetEncoding() const override { return lldb::eEncodingUint; }
  lldb::Format GetFormat() const override { return lldb::eFormatHex; }
  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassMemberPointer;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeIsPointer | lldb::eTypeIsMember | lldb::eTypeHasValue;
  }

private:
  TypeRef m_pointee_type;
  TypeRef m_containing_type;
};

/// Common base for "sugar" types that wrap another type and are transparent to
/// most layout/children queries: typedefs and cv-qualified types. Stripping all
/// sugar off a type yields its canonical type (see Type::GetCanonicalType-style
/// desugaring in TypeSystemCpp). The transparent virtual queries forward to the
/// underlying type; subclasses override the ones that must differ (e.g. a
/// typedef reports its own name and type class).
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

/// A typedef/using alias. It carries its own (alias) name but otherwise behaves
/// like the type it aliases.
class TypedefType : public llvm::RTTIExtends<TypedefType, SugarType> {
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
class EnumType : public llvm::RTTIExtends<EnumType, Type> {
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
  // Gated like RecordType's mutators: only Context (through the locked Builder)
  // may add enumerators.
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

  bool IsVariadic() const { return m_is_variadic; }
  void SetIsVariadic(bool is_variadic) { m_is_variadic = is_variadic; }

  lldb::TypeClass GetTypeClass() const override {
    return lldb::eTypeClassFunction;
  }
  uint32_t GetTypeInfo() const override {
    return lldb::eTypeIsFuncPrototype | lldb::eTypeHasValue;
  }

private:
  // Gated like the other mutators: only Context (through the locked Builder)
  // may add parameters.
  friend class Context;
  void AddParameter(TypeRef type) { m_params.push_back(type); }

  TypeRef m_return_type;
  std::vector<TypeRef> m_params;
  bool m_is_variadic = false;
};
} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_H
