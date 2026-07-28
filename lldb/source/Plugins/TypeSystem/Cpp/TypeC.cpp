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

uint32_t PointerType::GetTypeInfo() const {
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
