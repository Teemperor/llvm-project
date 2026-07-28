#include "TypeCpp.h"

#include "TypeC.h"

using namespace lldb_private::cpp_typesystem;

// Storage for the LLVM RTTI discriminators. The addresses (not the values) are
// what identify each class, so the initializer is irrelevant.
char ClassType::ID = 0;
char ReferenceType::ID = 0;
char MemberPointerType::ID = 0;

bool MemberPointerType::IsMemberFunctionPointer() const {
  return llvm::isa_and_nonnull<FunctionType>(cpp_typesystem::Desugar(GetPointeeType()));
}
