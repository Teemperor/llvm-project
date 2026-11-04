//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeC.h"

#include "Context.h"

#include "llvm/Support/FormatVariadic.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>

using namespace lldb_private::clike;

// Storage for the LLVM RTTI discriminators. The addresses (not the values) are
// what identify each class, so the initializer is irrelevant.
char StructType::ID = 0;
char ArrayType::ID = 0;
char PointerType::ID = 0;
char TypedefType::ID = 0;
char CVQualifiedType::ID = 0;
char EnumType::ID = 0;
char FunctionType::ID = 0;

std::optional<uint64_t> ArrayType::GetAlignmentInBits() const {
  if (!m_is_vector)
    return m_element_type.Get().GetAlignmentInBits();
  std::optional<uint64_t> byte_size = GetByteSize();
  if (!byte_size)
    return std::nullopt;
  uint64_t alignment =
      std::max<uint64_t>(llvm::PowerOf2Ceil(*byte_size * 8), 8);
  if (uint64_t max_alignment =
          GetOwningContext().GetLanguageOpts().GetMaxVectorAlignInBits())
    alignment = std::min(alignment, max_alignment);
  return alignment;
}

std::optional<uint64_t> PointerType::GetByteSize() const {
  // A pointer is the target's pointer width, taken from the Context that owns
  // this type -- so this works for a `void *` too, which has no pointee to ask.
  return GetOwningContext().GetPointerSize();
}

Type *PointerType::GetTransparentChildPointee() {
  Type *pointee = GetPointeeType();
  // `void *` falls out here rather than needing its own check: `void` is
  // not an aggregate.
  if (pointee->IsAggregate() && pointee->IsComplete())
    return pointee;
  return nullptr;
}

Type *PointerType::GetNamedMemberPointee() {
  Type *pointee = GetPointeeType();
  return pointee->IsAggregate() ? pointee : nullptr;
}

uint32_t PointerType::GetTypeInfo() const {
  return lldb::eTypeHasChildren | lldb::eTypeIsPointer | lldb::eTypeHasValue;
}

lldb::LanguageType PointerType::GetMinimumLanguage() const {
  const Type *pointee = GetPointeeType()->Desugar();
  return pointee->IsRecord() ? lldb::eLanguageTypeC_plus_plus
                             : lldb::eLanguageTypeC;
}

Type *PointerType::GetDynamicTypeCandidate(
    DynamicTypeCheck check, llvm::function_ref<void(Type *)> complete) {
  const bool check_cplusplus =
      (check & DynamicTypeCheck::CPlusPlus) != DynamicTypeCheck::None;
  Type *pointee = GetPointeeType()->Desugar();
  // `void *` is accepted as a possible (watered-down) dynamic pointer,
  // matching TypeSystemClang. This is accepted regardless of the
  // check flags (clang's Builtin::Void / UnknownAny
  // pointee case is not gated on them): an opaque pointer may point at either
  // a polymorphic C++ object or an ObjC object, and the ObjC runtime relies on
  // this to dynamic-type a `void *` exception pointer.
  if (pointee->IsVoid())
    return pointee;
  // A pointer to a class is dynamic iff the class is polymorphic.
  if (check_cplusplus && pointee->IsDynamicClass(complete))
    return pointee;
  return nullptr;
}

std::string
CVQualifiedType::GetDisplayName(const DisplayNameOptions &opts) const {
  std::string qualifier;
  if (m_is_const)
    qualifier += "const";
  if (m_is_volatile)
    qualifier += qualifier.empty() ? "volatile" : " volatile";
  std::string underlying = GetUnderlyingType()->GetDisplayName(opts);
  // A cv-qualifier on a pointer itself is a declarator suffix (`T *const`),
  // not a prefix (`const T *`, which would instead qualify the pointee) --
  // matching clang's own type printer. This only applies to the pointer
  // directly under this qualifier: a qualified pointee (`const T *const`) has
  // already rendered its own leading `const` as part of `underlying` (via the
  // pointee's own CVQualifiedType), so no double-prefixing happens here.
  if (GetUnderlyingType()->IsPointer()) {
    // No space between the `*` and the qualifier (`T *const`), matching
    // clang's declarator-suffix spelling.
    const bool tight = !underlying.empty() && underlying.back() == '*';
    return underlying + (tight ? "" : " ") + qualifier;
  }
  return qualifier + (qualifier.empty() ? "" : " ") + underlying;
}

std::string CVQualifiedType::GetCanonicalName(bool base_only) const {
  // The cv-qualifiers prefix the unqualified name, but only if the underlying
  // type spells them (see Type::SpellsCVQualifiersInCanonicalName).
  std::string underlying_name = GetUnderlyingType()->GetCanonicalName(base_only);
  if (!GetUnderlyingType()->SpellsCVQualifiersInCanonicalName())
    return underlying_name;
  std::string result;
  if (m_is_const)
    result += "const ";
  if (m_is_volatile)
    result += "volatile ";
  result += underlying_name;
  return result;
}

std::string PointerType::GetDisplayName(const DisplayNameOptions &opts) const {
  return GetPointeeType()->GetPointeeDisplayName("*", opts);
}

std::string PointerType::GetCanonicalName(bool base_only) const {
  return GetPointeeType()->GetPointeeCanonicalName("*", base_only);
}

// An unnamed enum (e.g. a function-local `enum { ... } x;`) has no spelling in
// the debug info. Match clang's TagDecl printing, which renders it as
// "(unnamed enum)" (see TagDecl::printName).
static constexpr llvm::StringLiteral g_unnamed_enum_name = "(unnamed enum)";

std::string EnumType::GetDisplayName(const DisplayNameOptions &opts) const {
  if (!IsUnnamed())
    return Type::GetDisplayName(opts);
  return g_unnamed_enum_name.str();
}

std::string EnumType::GetCanonicalName(bool base_only) const {
  if (!IsUnnamed())
    return Type::GetCanonicalName(base_only);
  return g_unnamed_enum_name.str();
}

std::string ArrayType::BuildName(
    llvm::function_ref<std::string(const Type &)> element_name) const {
  // A vector type (DW_AT_GNU_vector) is spelled like clang's
  // `<element> __attribute__((ext_vector_type(N)))` rather than as an array
  // (`<element>[N]`), so vector formatters match and array/char-array
  // formatters don't (matching TypeSystemClang's type name for vectors).
  if (m_is_vector)
    return llvm::formatv("{0} __attribute__((ext_vector_type({1})))",
                         element_name(*GetElementType()),
                         GetNumElements().value_or(0))
        .str();
  // For a multidimensional C array the DWARF/type nesting goes
  // outermost-dimension first (`T[2][3]` == array-of-2 of array-of-3 of T),
  // but the element name must be printed innermost-last. Peel the whole
  // array chain collecting dimensions left-to-right, then append them after
  // the innermost element's name so the brackets keep their source order.
  std::string dims;
  const Type *cur = this;
  while (const Type *element = cur->GetArrayElementType()) {
    if (std::optional<uint64_t> n = cur->GetNumElements())
      dims += llvm::formatv("[{0}]", *n).str();
    else
      dims += "[]";
    cur = element;
  }
  return element_name(*cur) + dims;
}

std::string ArrayType::GetDisplayName(const DisplayNameOptions &opts) const {
  return BuildName([&](const Type &t) { return t.GetDisplayName(opts); });
}

std::string ArrayType::GetCanonicalName(bool base_only) const {
  return BuildName([&](const Type &t) { return t.GetCanonicalName(base_only); });
}

std::string FunctionType::GetDeclaratorName(llvm::StringRef decl,
                                            bool keep_inline_namespaces) const {
  DisplayNameOptions opts;
  opts.keep_inline_namespaces = keep_inline_namespaces;
  // A function that returns nothing returns the `void` builtin, so there is
  // always a return type to name.
  std::string ret = GetReturnType()->GetDisplayName(opts);
  std::string params;
  for (const TypeRef &param : m_params) {
    if (!params.empty())
      params += ", ";
    params += param.Get().GetDisplayName(opts);
  }
  if (m_is_variadic)
    params += params.empty() ? "..." : ", ...";
  // An empty, prototyped parameter list is rendered as `()` in C++ (matching
  // clang's TypePrinter with UseVoidForZeroParams=false) but as `(void)` in C,
  // where an empty parameter list without `void` instead means "unspecified
  // parameters" (K&R). See UseVoidForEmptyParams for how that bit is
  // determined at parse time. An unprototyped (K&R) function has a
  // DW_TAG_unspecified_parameters child and is modelled as variadic above, so
  // it prints as `(...)` regardless.
  else if (params.empty() && m_use_void_for_empty_params)
    params = "void";
  return ret + " " + decl.str() + "(" + params + ")";
}

std::string
FunctionType::GetDisplayName(const DisplayNameOptions &opts) const {
  return GetDeclaratorName("", opts.keep_inline_namespaces);
}

// Function types and pointers to them need C declarator syntax
// (`int (*)(const char *)`), which the display name already produces.
std::string FunctionType::GetCanonicalName(bool base_only) const {
  return GetDisplayName(DisplayNameOptions());
}

std::string
FunctionType::GetPointeeDisplayName(llvm::StringRef sigil,
                                    const DisplayNameOptions &opts) const {
  return GetDeclaratorName(("(" + sigil + ")").str(),
                           opts.keep_inline_namespaces);
}

std::string FunctionType::GetPointeeCanonicalName(llvm::StringRef sigil,
                                                  bool base_only) const {
  return GetPointeeDisplayName(sigil, DisplayNameOptions());
}
