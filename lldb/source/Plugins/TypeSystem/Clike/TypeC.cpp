//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeC.h"

#include "Context.h"

using namespace lldb_private::clike;

// Storage for the LLVM RTTI discriminators. The addresses (not the values) are
// what identify each class, so the initializer is irrelevant.
char PointerType::ID = 0;
char TypedefType::ID = 0;
char CVQualifiedType::ID = 0;

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
