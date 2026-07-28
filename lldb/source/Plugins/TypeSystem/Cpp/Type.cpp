#include "Type.h"

using namespace lldb_private::cpp_typesystem;

// Storage for the LLVM RTTI discriminators of the language-neutral core types.
// The addresses (not the values) are what identify each class, so the
// initializer is irrelevant. The per-language kinds define theirs in TypeC.cpp,
// TypeCpp.cpp and TypeObjC.cpp.
char Type::ID = 0;
char RecordType::ID = 0;
char SugarType::ID = 0;
