//===-- TypeCpp.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The C++-only type kinds and the C++-only pieces of a record (base classes,
// template arguments, member functions, static data members). See Type.h for
// the language-neutral core and TypeC.h / TypeObjC.h for the other languages.
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_CPP_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_CPP_H

#include <cassert>
#include <cstdint>
#include <optional>
#include <vector>

#include "llvm/Support/Casting.h"
#include "llvm/Support/ExtensibleRTTI.h"

#include "lldb/lldb-enumerations.h"

#include "Identifier.h"
#include "Type.h"

namespace lldb_private {
namespace cpp_typesystem {

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

/// A template argument of a class template instantiation (e.g. the `int` and
/// `std::allocator<int>` of `std::vector<int, std::allocator<int>>`). Data
/// formatters rely on these to recover, for instance, a container's element
/// type.
struct TemplateArgument {
  /// Type argument: the argument's type. Integral argument: the value's type.
  TypeRef type;
  /// Template argument (a template-template parameter, e.g. the `T1` in
  /// `C<float, T1>`): the referenced template's name. Such arguments are not a
  /// modeled type, so only their spelling is kept -- enough to reconstruct the
  /// instantiation's display name.
  Identifier name;
  /// Integral argument: the raw value bits (interpret using `type`'s
  /// signedness/size).
  uint64_t integral_value = 0;
  // The small members are last so they pack together instead of forcing
  // padding between the pointer-sized members above.
  lldb::TemplateArgumentKind kind = lldb::eTemplateArgumentKindNull;
  /// True when this argument was defaulted (DWARF's DW_AT_default_value), so it
  /// is hidden when building the type's display name (`std::vector<int>` rather
  /// than `std::vector<int, std::allocator<int>>`).
  bool is_default = false;
};

/// The ref-qualifier applied to a member function's implicit object parameter
/// (the `&`/`&&` in `void f() &` / `void f() &&`). A class can overload on this,
/// so it must be modeled to disambiguate calls.
enum class RefQualifier : uint8_t {
  None,   ///< No ref-qualifier (`void f()`).
  LValue, ///< Lvalue ref-qualifier (`void f() &`).
  RValue, ///< Rvalue ref-qualifier (`void f() &&`).
};

/// Whether a member function is an ordinary method or a special member the
/// expression evaluator must build with a proper C++ declaration name (a
/// constructor/destructor uses a getCXXConstructorName/getCXXDestructorName,
/// not an identifier). Detected from DWARF (see
/// DWARFASTParserCpp::CompleteMemberFunctionsFromDWARF).
enum class MemberFunctionKind : uint8_t {
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
  bool is_static : 1;
  bool is_const : 1;
  bool is_volatile : 1;
  bool is_virtual : 1;
  /// The `&`/`&&` ref-qualifier, if any (overloadable, so must be modeled).
  RefQualifier ref_qualifier : 2;
  /// Whether this is an ordinary method, a constructor or a destructor. A
  /// constructor/destructor must be built with the proper C++ declaration name
  /// so clang recognizes `Foo(2)` as a constructor call.
  MemberFunctionKind kind : 2;

  // The flags above are packed bit-fields, which can't carry default member
  // initializers before C++20 (LLVM builds as C++17), so they are defaulted
  // here.
  MemberFunction()
      : is_static(false), is_const(false), is_volatile(false),
        is_virtual(false), ref_qualifier(RefQualifier::None),
        kind(MemberFunctionKind::Method) {}
};

/// A C++ class type. In addition to the data members every record has, it can
/// carry direct base classes.
class ClassType : public llvm::RTTIExtends<ClassType, RecordType> {
public:
  static char ID;

  // m_is_polymorphic/m_is_template are bit-fields (see RecordType's ctor note).
  ClassType() : m_is_polymorphic(false), m_is_template(false) {}

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

  // Template arguments, member functions and static data members are C++-only,
  // so their storage lives here (not on RecordType, where a plain C struct or
  // an Objective-C interface would needlessly reserve it).
  bool IsTemplateInstantiation() const override { return m_is_template; }

  uint32_t GetNumTemplateArguments() const override {
    return m_template_args.size();
  }
  const TemplateArgument *
  GetTemplateArgumentAtIndex(uint32_t idx) const override {
    if (idx < m_template_args.size())
      return &m_template_args[idx];
    return nullptr;
  }

  uint32_t GetNumMemberFunctions() const override { return m_methods.size(); }
  const MemberFunction *GetMemberFunctionAtIndex(uint32_t idx) const override {
    if (idx < m_methods.size())
      return &m_methods[idx];
    return nullptr;
  }

  uint32_t GetNumStaticDataMembers() const override {
    return m_static_data_members.size();
  }
  const StaticDataMember *
  GetStaticDataMemberAtIndex(uint32_t idx) const override {
    if (idx < m_static_data_members.size())
      return &m_static_data_members[idx];
    return nullptr;
  }

private:
  // Gated like RecordType's mutators (see there): only Context, reached through
  // a Builder, may mutate a class.
  friend class Context;
  void AddBaseClass(TypeRef type, uint64_t byte_offset,
                    bool is_virtual = false,
                    std::optional<uint64_t> vbase_offset_offset = std::nullopt) {
    m_bases.push_back(
        BaseClass{type, byte_offset, is_virtual, vbase_offset_offset});
  }
  void SetPolymorphic() { m_is_polymorphic = true; }
  void SetIsTemplateInstantiation(bool is_template) {
    m_is_template = is_template;
  }
  void AddTemplateArgument(TemplateArgument arg) {
    m_template_args.push_back(arg);
  }
  void AddMemberFunction(MemberFunction method) { m_methods.push_back(method); }
  void AddStaticDataMember(StaticDataMember member) {
    m_static_data_members.push_back(member);
  }

  std::vector<BaseClass> m_bases;
  std::vector<TemplateArgument> m_template_args;
  std::vector<MemberFunction> m_methods;
  std::vector<StaticDataMember> m_static_data_members;
  bool m_is_polymorphic : 1;
  bool m_is_template : 1;
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

  // A reference is transparent: its children are those of the referenced type.
  // Like a pointer, this must not force completion of the referent.
  Type *GetTransparentChildPointee() override {
    Type *pointee = m_pointee_type.Get();
    if (pointee && pointee->IsAggregate() && pointee->IsComplete())
      return pointee;
    return nullptr;
  }
  Type *GetNamedMemberPointee() override {
    Type *pointee = m_pointee_type.Get();
    return pointee && pointee->IsAggregate() ? pointee : nullptr;
  }

  // A reference is represented by an address, so it has the (small) target
  // pointer width. Stored compactly like PointerType's size (see there).
  std::optional<uint64_t> GetByteSize() const override {
    return m_byte_size == 0 ? std::nullopt
                            : std::optional<uint64_t>(m_byte_size);
  }
  void SetByteSize(std::optional<uint64_t> byte_size) {
    assert(byte_size.value_or(0) <= UINT8_MAX && "reference size out of range");
    m_byte_size = static_cast<uint8_t>(byte_size.value_or(0));
  }

  // Like a pointer, a reference is pointer-aligned (see
  // PointerType::GetAlignmentInBits).
  std::optional<uint64_t> GetAlignmentInBits() const override {
    if (std::optional<uint64_t> size = GetByteSize())
      return *size * 8;
    return std::nullopt;
  }

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
  uint8_t m_byte_size = 0;
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

  /// True for a pointer to a member *function* (`R (C::*)(Args...)`), false for
  /// a pointer to a data member (`T C::*`). Out-of-line: telling them apart
  /// needs FunctionType, which lives in TypeC.h.
  bool IsMemberFunctionPointer() const;

  // ABI-defined width (one or two pointers); small, so stored compactly like
  // PointerType's size (see there).
  std::optional<uint64_t> GetByteSize() const override {
    return m_byte_size == 0 ? std::nullopt
                            : std::optional<uint64_t>(m_byte_size);
  }
  void SetByteSize(std::optional<uint64_t> byte_size) {
    assert(byte_size.value_or(0) <= UINT8_MAX &&
           "member-pointer size out of range");
    m_byte_size = static_cast<uint8_t>(byte_size.value_or(0));
  }

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
  uint8_t m_byte_size = 0;
};

} // namespace cpp_typesystem
} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CPP_TYPE_CPP_H
