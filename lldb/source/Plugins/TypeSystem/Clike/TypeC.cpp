#include "TypeC.h"

#include "Context.h"
#include "TypeObjC.h"

using namespace lldb_private::clike_typesystem;

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

std::optional<uint64_t> PointerType::GetByteSize() const {
  // A pointer is the target's pointer width, taken from the Context that owns
  // this type -- so this works for a `void *` too, which has no pointee to ask.
  return GetOwningContext().GetPointerSize();
}

unsigned CVQualifiedType::GetCVRMask(const Type *t) {
  unsigned quals = 0;
  // A stand-in for a type another Context owns carries no qualifiers of its own
  // (see ForeignType), so peel one at every step of the chain -- otherwise a
  // `volatile` below it would be missed.
  t = ForeignType::Strip(t);
  while (auto *cv = llvm::dyn_cast_or_null<CVQualifiedType>(t)) {
    if (cv->IsConst())
      quals |= 0x1;
    if (cv->IsVolatile())
      quals |= 0x4;
    t = ForeignType::Strip(cv->GetUnderlyingType());
  }
  return quals;
}

bool PointerType::IsFunctionPointer() const {
  return !llvm::isa<BlockPointerType>(this) &&
         llvm::isa_and_nonnull<FunctionType>(clike_typesystem::Desugar(GetPointeeType()));
}

Type *PointerType::GetTransparentChildPointee() {
  Type *pointee = m_pointee_type.GetOrNone();
  if (!pointee)
    return nullptr; // `void *` has no children.
  // A pointer to an ObjC interface is always transparent: an ObjC object is
  // only ever reached through a pointer, so its children are the interface's
  // ivars/superclass. (The interface is completed by the caller, which has the
  // SymbolFile.)
  if (llvm::isa<ObjCInterfaceType>(pointee->Desugar()))
    return pointee;
  if (pointee->IsAggregate() && pointee->IsComplete())
    return pointee;
  return nullptr;
}

Type *PointerType::GetNamedMemberPointee() {
  Type *pointee = m_pointee_type.GetOrNone();
  return pointee && pointee->IsAggregate() ? pointee : nullptr;
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
  if (IsObjCObjectType(m_pointee_type.GetOrNone()))
    info |= lldb::eTypeIsObjC;
  return info;
}
