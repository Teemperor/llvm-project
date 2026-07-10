#include "Type.h"

using namespace lldb_private::cpp_typesystem;

// Storage for the LLVM RTTI discriminators. The addresses (not the values) are
// what identify each class, so the initializer is irrelevant.
char Type::ID = 0;
char RecordType::ID = 0;
char StructType::ID = 0;
char ClassType::ID = 0;
char PointerType::ID = 0;
