#include "TypeObjC.h"

using namespace lldb_private::cpp_typesystem;

// Storage for the LLVM RTTI discriminator. The address (not the value) is what
// identifies the class, so the initializer is irrelevant.
char ObjCInterfaceType::ID = 0;

bool lldb_private::cpp_typesystem::IsOpaqueObjCObjectRecord(const Type *t) {
  auto *rec = llvm::dyn_cast_or_null<RecordType>(t);
  if (!rec)
    return false;
  llvm::StringRef name = rec->GetName().GetName();
  return name == "objc_object" || name == "objc_class";
}

bool lldb_private::cpp_typesystem::IsObjCObjectType(const Type *t) {
  return llvm::isa_and_nonnull<ObjCInterfaceType>(t) ||
         IsOpaqueObjCObjectRecord(t);
}
