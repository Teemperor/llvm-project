#include "Type.h"

#include "Context.h"

using namespace lldb_private::cpp_typesystem;

// Storage for the LLVM RTTI discriminators. The addresses (not the values) are
// what identify each class, so the initializer is irrelevant.
char Type::ID = 0;
char RecordType::ID = 0;
char StructType::ID = 0;
char ClassType::ID = 0;
char ObjCInterfaceType::ID = 0;
char ArrayType::ID = 0;
char PointerType::ID = 0;
char BlockPointerType::ID = 0;
char ReferenceType::ID = 0;
char MemberPointerType::ID = 0;
char SugarType::ID = 0;
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
