//===-- TypeC.cpp ---------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeC.h"

#include "Context.h"
#include "TypeObjC.h"

#include "llvm/Support/MathExtras.h"

#include <algorithm>

using namespace lldb_private::clike;

// Storage for the LLVM RTTI discriminators. The addresses (not the values) are
// what identify each class, so the initializer is irrelevant.
char StructType::ID = 0;
char ArrayType::ID = 0;
char PointerType::ID = 0;
char BlockPointerType::ID = 0;
char TypedefType::ID = 0;
char CVQualifiedType::ID = 0;
char PtrAuthType::ID = 0;
char ElaboratedType::ID = 0;
char EnumType::ID = 0;
char FunctionType::ID = 0;
char ComplexType::ID = 0;

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
  // A pointer to an ObjC interface is always transparent: an ObjC object is
  // only ever reached through a pointer, so its children are the interface's
  // ivars/superclass. (The interface is completed by the caller, which has the
  // SymbolFile.)
  if (pointee->Desugar()->IsObjCInterface())
    return pointee;
  // `void *` falls out here rather than needing its own check: `void` is
  // neither an aggregate nor an ObjC interface.
  if (pointee->IsAggregate() && pointee->IsComplete())
    return pointee;
  return nullptr;
}

Type *PointerType::GetNamedMemberPointee() {
  Type *pointee = GetPointeeType();
  return pointee->IsAggregate() ? pointee : nullptr;
}

uint32_t PointerType::GetTypeInfo() const {
  uint32_t info =
      lldb::eTypeHasChildren | lldb::eTypeIsPointer | lldb::eTypeHasValue;
  // A pointer to an Objective-C object (`Foo *` / `id` / `Class`) is itself an
  // Objective-C construct: report eTypeIsObjC so that the value printer's
  // ObjC pointer-expansion (dwim-print's SetExpandPointerTypeFlags) and the
  // ObjC language runtime treat it as an object pointer, and so e.g.
  // ObjCLanguage::IsNilReference prints a null `id` as "nil" instead of "0x0".
  // (Sugar between the pointer and the interface is peeled by the caller via
  // Desugar; the pointee stored here is normally the interface directly.)
  if (IsObjCObjectType(GetPointeeType()))
    info |= lldb::eTypeIsObjC;
  return info;
}

lldb::LanguageType PointerType::GetMinimumLanguage() const {
  const Type *pointee = GetPointeeType()->Desugar();
  // An ObjC object pointer (`NSObject *`, or the `id`/`Class` idiom -- a
  // pointer to the opaque `objc_object`/`objc_class` record) is routed to the
  // ObjC runtime for dynamic-type resolution.
  if (IsObjCObjectType(pointee))
    return lldb::eLanguageTypeObjC;
  return pointee->IsRecord() ? lldb::eLanguageTypeC_plus_plus
                             : lldb::eLanguageTypeC;
}

Type *PointerType::GetDynamicTypeCandidate(
    bool check_cplusplus, bool check_objc,
    llvm::function_ref<void(Type *)> complete) {
  Type *pointee = GetPointeeType()->Desugar();
  // An Objective-C object is always accessed through a pointer (`Foo *` /
  // `id`), so a pointer to an ObjC interface is a possible dynamic type when
  // checking ObjC -- and never a C++ one.
  if (pointee->IsObjCInterface())
    return check_objc ? pointee : nullptr;
  // `id` is modeled as a pointer to the opaque `objc_object` record.
  if (check_objc && IsOpaqueObjCObjectRecord(pointee))
    return pointee;
  // `void *` is accepted as a possible (watered-down) dynamic pointer,
  // matching TypeSystemClang. This is accepted regardless of the
  // check_cplusplus / check_objc flags (clang's Builtin::Void / UnknownAny
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
