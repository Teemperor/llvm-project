#include "Type.h"

using namespace lldb_private::clike_typesystem;

// Storage for the LLVM RTTI discriminators of the language-neutral core types.
// The addresses (not the values) are what identify each class, so the
// initializer is irrelevant. The per-language kinds define theirs in TypeC.cpp,
// TypeCpp.cpp and TypeObjC.cpp.
char Type::ID = 0;
char RecordType::ID = 0;
char SugarType::ID = 0;

std::optional<uint64_t> Type::GetAlignmentInBits() const {
  // Prefer an explicitly-recorded alignment (e.g. an `alignas(...)` type, whose
  // DW_AT_alignment the DWARF parser stored), which the size-derived heuristic
  // below cannot recover.
  if (std::optional<uint64_t> align = GetAlignInBits())
    if (*align != 0)
      return *align;

  std::optional<uint64_t> byte_size = GetByteSize();
  if (!byte_size || *byte_size == 0)
    return std::nullopt;
  uint64_t align_bytes = 1;
  while (align_bytes * 2 <= 8 && (*byte_size % (align_bytes * 2)) == 0)
    align_bytes *= 2;
  return align_bytes * 8;
}

bool RecordType::HasFields(Type *t,
                           llvm::function_ref<void(Type *)> complete) {
  t = clike_typesystem::Desugar(t);
  if (!t)
    return false;
  complete(t);
  if (t->GetNumFields() != 0)
    return true;
  // We always want a record with no definition anywhere in the debug info
  // (e.g. -flimit-debug-info) to show up, so we can print a message in the
  // summary indicating that the type is incomplete: otherwise a base class
  // in this state would be silently hidden by the omit-empty-base-classes
  // logic (since it looks exactly like an empty-but-complete base), and a
  // top-level variable of such a type would show nothing at all. Mirrors
  // TypeSystemClang::RecordHasFields's IsForcefullyCompleted check.
  if (!t->IsComplete())
    return true;
  for (uint32_t i = 0, n = t->GetNumBaseClasses(); i < n; ++i) {
    const BaseClass *base = t->GetBaseClassAtIndex(i);
    if (base && HasFields(&base->type.Get(), complete))
      return true;
  }
  return false;
}
