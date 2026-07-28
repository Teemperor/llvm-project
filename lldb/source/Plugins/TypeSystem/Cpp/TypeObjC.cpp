#include "TypeObjC.h"

using namespace lldb_private::cpp_typesystem;

// Storage for the LLVM RTTI discriminator. The address (not the value) is what
// identifies the class, so the initializer is irrelevant.
char ObjCInterfaceType::ID = 0;
