#include "TypeC.h"

#include "Context.h"
#include "TypeObjC.h"

using namespace lldb_private::cpp_typesystem;

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
  // A pointer is the target's pointer width. Recover it from the Context that
  // owns the pointee reference (Context::CreatePointerType guarantees this
  // reference always carries a Context, even for a `void *` with no pointee).
  if (const Context *ctx = m_pointee_type.GetContext())
    return ctx->GetPointerSize();
  return std::nullopt;
}

unsigned CVQualifiedType::GetCVRMask(const Type *t) {
  unsigned quals = 0;
  while (auto *cv = llvm::dyn_cast_or_null<CVQualifiedType>(t)) {
    if (cv->IsConst())
      quals |= 0x1;
    if (cv->IsVolatile())
      quals |= 0x4;
    t = cv->GetUnderlyingType();
  }
  return quals;
}

Type *PointerType::GetTransparentChildPointee() {
  Type *pointee = m_pointee_type.Get();
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
  Type *pointee = m_pointee_type.Get();
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
  if (IsObjCObjectType(m_pointee_type.Get()))
    info |= lldb::eTypeIsObjC;
  return info;
}
