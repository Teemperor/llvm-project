//===-- TypeSystemClike.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemClike.h"

#include <optional>

#include "TypeName.h"

#include "Plugins/SymbolFile/DWARF/DWARFASTParserClike.h"
#include "Plugins/SymbolFile/DWARF/DWARFDIE.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/Symbol.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Type.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/Scalar.h"
#include "lldb/Utility/Stream.h"
#include "lldb/ValueObject/ValueObject.h"

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cstdio>

using namespace lldb_private;
using namespace lldb;

using clike::Field;

using clike::Desugar;

LLDB_PLUGIN_DEFINE(TypeSystemClike)

char TypeSystemClike::ID;

TypeSystemClike::TypeSystemClike(llvm::StringRef name, clike::LanguageOpts opts)
    : m_display_name(name.str()), m_triple(opts.GetTriple()),
      m_context(std::move(opts)) {}

TypeSystemClike::~TypeSystemClike() = default;

//===----------------------------------------------------------------------===//
// Cross-instance locking (see the locking note in TypeSystemClike.h).
//===----------------------------------------------------------------------===//

namespace {
/// Which instances' locks this thread currently holds, and in which mode, so
/// LockSet can tell a harmless nested acquisition from one that will deadlock.
///
/// Worth the bookkeeping because the platform hides both cases rather than
/// exposing them: llvm::sys::RWMutex is pthread_rwlock_t on Darwin
/// (LLVM_USE_RW_MUTEX_IMPL, see llvm/Support/RWMutex.h), pthread_rwlock_rdlock
/// / _wrlock return EDEADLK when the calling thread already holds the write
/// lock, and SmartRWMutex discards that error -- so a re-entrant acquisition
/// here silently does not lock and execution carries on with no mutual
/// exclusion at all, while the same code deadlocks on a host where RWMutex is
/// std::shared_mutex. Neither shows up as a test failure, so these checks are
/// the only thing keeping the locking discipline honest.
#ifndef NDEBUG
struct HeldLock {
  const TypeSystemClike *ts;
  bool exclusive;
};
thread_local llvm::SmallVector<HeldLock, 8> g_thread_held_locks;

/// The mode this thread already holds \p ts in, or std::nullopt.
std::optional<bool> HeldModeByThisThread(const TypeSystemClike *ts) {
  for (const HeldLock &held : g_thread_held_locks)
    if (held.ts == ts)
      return held.exclusive;
  return std::nullopt;
}
/// Whether this thread holds a lock on an instance ordered *after* \p ts, so
/// that acquiring \p ts now would take the global order backwards.
bool HoldsLockOrderedAfter(const TypeSystemClike *ts) {
  for (const HeldLock &held : g_thread_held_locks)
    if (held.ts > ts)
      return true;
  return false;
}
void NoteLockAcquired(const TypeSystemClike *ts, bool exclusive) {
  g_thread_held_locks.push_back({ts, exclusive});
}
void NoteLockReleased(const TypeSystemClike *ts) {
  for (auto it = g_thread_held_locks.rbegin(); it != g_thread_held_locks.rend();
       ++it)
    if (it->ts == ts) {
      g_thread_held_locks.erase(std::next(it).base());
      return;
    }
  llvm_unreachable("releasing a lock this thread never acquired");
}
#else
std::optional<bool> HeldModeByThisThread(const TypeSystemClike *) {
  return std::nullopt;
}
bool HoldsLockOrderedAfter(const TypeSystemClike *) { return false; }
void NoteLockAcquired(const TypeSystemClike *, bool) {}
void NoteLockReleased(const TypeSystemClike *) {}
#endif
} // namespace

TypeSystemClike::LockSet::LockSet(llvm::ArrayRef<TypeSystemClike *> ordered,
                                  bool exclusive)
    : m_exclusive(exclusive) {
  m_locked.reserve(ordered.size());
  for (TypeSystemClike *ts : ordered) {
    assert((m_locked.empty() || m_locked.back() < ts) &&
           "lock order must be strictly ascending by address -- that single "
           "global order is what keeps overlapping sets deadlock-free");
    // Nested acquisition of a lock this thread already holds is harmless
    // *provided* it already holds it at least as strongly: the access being
    // asked for is already ours, so skip it rather than re-take it (which the
    // mutex cannot express). This happens for real and legitimately -- a query
    // holding the write lock hands a CompilerType to a data formatter or to the
    // Objective-C runtime, which calls back in through the public API.
    if (std::optional<bool> held = HeldModeByThisThread(ts)) {
      assert((!exclusive || *held) &&
             "cannot upgrade a reader lock this thread already holds to a "
             "writer lock -- the outer entry point has to take the write lock");
      continue;
    }
    // Taking a *new* lock while already holding one ordered after it runs the
    // global order backwards, which is the cycle the ordering exists to
    // prevent. The fix is never to lock here: it is for the outer entry point
    // to have included this instance in its own set (see GetLockOrder) and for
    // this code to reach it through a non-locking entry point.
    assert(!HoldsLockOrderedAfter(ts) &&
           "acquiring this instance's lock would take the global lock order "
           "backwards -- see TypeSystemClike::GetLockOrder");
    if (exclusive)
      ts->m_mutex.lock();
    else
      ts->m_mutex.lock_shared();
    // Recorded only after the lock is held, so Release() unlocks exactly what
    // was acquired even if a lock throws.
    m_locked.push_back(ts);
    NoteLockAcquired(ts, exclusive);
  }
}

void TypeSystemClike::LockSet::Release() {
  // Reverse order, mirroring how nested scoped locks unwind.
  for (TypeSystemClike *ts : llvm::reverse(m_locked)) {
    NoteLockReleased(ts);
    if (m_exclusive)
      ts->m_mutex.unlock();
    else
      ts->m_mutex.unlock_shared();
  }
  m_locked.clear();
}

void TypeSystemClike::AppendLockOrder(
    llvm::SmallVectorImpl<TypeSystemClike *> &out) const {
  out.push_back(const_cast<TypeSystemClike *>(this));
}

llvm::SmallVector<TypeSystemClike *, 4> TypeSystemClike::GetLockOrder() const {
  std::lock_guard<std::mutex> guard(m_lock_order_mutex);
  if (m_lock_order_valid && !HasDynamicLockOrder())
    return m_lock_order;

  m_lock_order.clear();
  AppendLockOrder(m_lock_order);
  // Sort by address and dedup: this is the global order LockSet relies on, and
  // AppendLockOrder is free to report an instance more than once (several
  // compile units importing the same module, say).
  llvm::sort(m_lock_order);
  m_lock_order.erase(llvm::unique(m_lock_order), m_lock_order.end());
  assert(llvm::is_contained(m_lock_order, this) &&
         "an instance must always lock itself");
  m_lock_order_valid = true;
  return m_lock_order;
}

plugin::dwarf::DWARFASTParser *TypeSystemClike::GetDWARFParser() {
  if (!m_dwarf_ast_parser_up)
    m_dwarf_ast_parser_up = std::make_unique<DWARFASTParserClike>(*this);
  return m_dwarf_ast_parser_up.get();
}

CompilerType TypeSystemClike::GetCompilerType(clike::Type *type) {
  return CompilerType(weak_from_this(), type);
}

llvm::Expected<lldb::TypeSystemSP>
TypeSystemClike::Create(llvm::StringRef name, llvm::Triple triple) {
  llvm::Expected<clike::LanguageOpts> opts =
      clike::LanguageOpts::Create(std::move(triple));
  if (!opts)
    return opts.takeError();
  return std::make_shared<TypeSystemClike>(name, std::move(*opts));
}

TypeSystemSP TypeSystemClike::CreateInstance(LanguageType language,
                                             Module *module, Target *target) {
  return TypeSystemSP();
}

LanguageSet TypeSystemClike::GetSupportedLanguagesForTypes() {
  return LanguageSet();
}

LanguageSet TypeSystemClike::GetSupportedLanguagesForExpressions() {
  return LanguageSet();
}

void TypeSystemClike::Initialize() {
  PluginManager::RegisterPlugin(GetPluginNameStatic(),
                                "C/C++/Objective-C++ TypeSystem plug-in",
                                CreateInstance, GetSupportedLanguagesForTypes(),
                                GetSupportedLanguagesForExpressions());
}

void TypeSystemClike::Terminate() {
  PluginManager::UnregisterPlugin(CreateInstance);
}

ConstString TypeSystemClike::DeclGetName(void *opaque_decl) {
  // TypeSystemClike doesn't hand out any CompilerDecls yet.
  return ConstString();
}

CompilerType TypeSystemClike::GetTypeForDecl(void *opaque_decl) {
  return CompilerType();
}

ConstString TypeSystemClike::DeclContextGetName(void *opaque_decl_ctx) {
  return ConstString();
}

ConstString
TypeSystemClike::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  return ConstString();
}

bool TypeSystemClike::DeclContextIsClassMethod(void *opaque_decl_ctx) {
  return false;
}

bool TypeSystemClike::DeclContextIsContainedInLookup(
    void *opaque_decl_ctx, void *other_opaque_decl_ctx) {
  return false;
}

LanguageType TypeSystemClike::DeclContextGetLanguage(void *opaque_decl_ctx) {
  return eLanguageTypeC_plus_plus;
}

#ifndef NDEBUG
bool TypeSystemClike::Verify(opaque_compiler_type_t type) { return true; }
#endif

bool TypeSystemClike::IsArrayType(lldb::opaque_compiler_type_t type,
                                  CompilerType *element_type, uint64_t *size,
                                  bool *is_incomplete) {
  return false;
}

bool TypeSystemClike::IsAggregateType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && t->IsAggregate();
}

bool TypeSystemClike::IsCharType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  // Strip typedef/cv sugar so `const char` (e.g. the pointee of `const char *`)
  // is recognized.
  return Desugar(t.get())->IsChar();
}

bool TypeSystemClike::IsCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // Mirror TypeSystemClang: complete the type now (if it has a definition in
  // the debug info) so we can give the caller an accurate answer about whether
  // the type actually has a definition, rather than just its current internal
  // completeness state. Builtins, pointers, references, etc. report complete
  // via clike::Type::IsComplete(); only records/enums that were parsed
  // as forward declarations without a definition stay incomplete.
  return GetCompleteType(type);
}

bool TypeSystemClike::IsDefined(opaque_compiler_type_t type) { return false; }

bool TypeSystemClike::IsFloatingPointType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && t->GetEncoding() == eEncodingIEEE754;
}

bool TypeSystemClike::IsFunctionType(lldb::opaque_compiler_type_t type) {
  return false;
}

size_t TypeSystemClike::GetNumberOfFunctionArguments(
    lldb::opaque_compiler_type_t type) {
  return 0;
}

CompilerType
TypeSystemClike::GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                            const size_t index) {
  return CompilerType();
}

bool TypeSystemClike::IsFunctionPointerType(lldb::opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsMemberFunctionPointerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  return Desugar(t.get())->IsMemberFunctionPointer();
}

bool TypeSystemClike::IsMemberDataPointerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  return Desugar(t.get())->IsMemberDataPointer();
}

bool TypeSystemClike::IsBlockPointerType(
    lldb::opaque_compiler_type_t type,
    CompilerType *function_pointer_type_ptr) {
  return false;
}

bool TypeSystemClike::IsIntegerType(opaque_compiler_type_t type,
                                    bool &is_signed) {
  is_signed = false;
  SharedLockedType tt = GetTypeForRead(type);
  return tt && IsIntegerTypeImpl(tt.get(), is_signed);
}

bool TypeSystemClike::IsIntegerTypeImpl(const clike::Type *t, bool &is_signed) {
  // Only builtin integer types (and enumerations, matching TypeSystemClike's
  // existing treatment) are integers; see clike::Type::IsInteger.
  t = Desugar(t);
  if (!t->IsInteger()) {
    is_signed = false;
    return false;
  }
  is_signed = t->GetEncoding() == eEncodingSint;
  return true;
}

bool TypeSystemClike::IsScopedEnumerationType(
    lldb::opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                                            CompilerType *target_type,
                                            bool check_cplusplus,
                                            bool check_objc) {
  return false;
}

bool TypeSystemClike::IsPointerType(lldb::opaque_compiler_type_t type,
                                    CompilerType *pointee_type) {
  return false;
}

bool TypeSystemClike::IsScalarType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && (t->GetTypeInfo() & eTypeIsScalar);
}

bool TypeSystemClike::IsVoidType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  return clike::IsVoid(t.get());
}

bool TypeSystemClike::CanPassInRegisters(const CompilerType &type) {
  return false;
}

bool TypeSystemClike::SupportsLanguage(LanguageType language) {
  return Language::LanguageIsCFamily(language);
}

bool TypeSystemClike::GetCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  LockedType t = GetTypeForWrite(type);
  clike::Type *completed = CompleteTypeAssumingWriteLocked(t.get());
  return completed && completed->IsComplete();
}

clike::Type *
TypeSystemClike::CompleteTypeAssumingWriteLocked(clike::Type *type) {
  if (!type)
    return nullptr;
  // See through typedefs/qualifiers: it is the underlying record that carries
  // completion state and is registered in the forward-declaration map.
  clike::Type *t = Desugar(type);
  if (!t)
    return nullptr;
  if (t->IsComplete())
    return t;
  // Ask our SymbolFile to fill in the members from the debug info.
  if (SymbolFile *sym_file = GetSymbolFile()) {
    CompilerType ct = GetCompilerType(t);
    sym_file->CompleteType(ct);
  }
  // A record/enum that is still incomplete after asking the SymbolFile means
  // no definition could be found anywhere (e.g. -flimit-debug-info stripped it
  // from this module). We intentionally leave it incomplete rather than
  // forcefully completing it as an empty definition (unlike TypeSystemClang's
  // RequireCompleteType), but still record the same statistics signal
  // (GetHasForcefullyCompletedTypes) so `debugInfoHadIncompleteTypes` is
  // reported correctly.
  if (!t->IsComplete())
    m_has_forcefully_completed_types = true;
  return t;
}

uint32_t TypeSystemClike::GetPointerByteSize() {
  return m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size;
}

CompilerType TypeSystemClike::GetSizeType() {
  // The type of a `sizeof` result. Clang spells this builtin `__size_t`, sized
  // as a pointer, so model it as a bespoke builtin of that name and width --
  // exactly as GetPointerDiffType does for `__ptrdiff_t` -- rather than as the
  // canonical unsigned type of the same width, so a value object created from
  // it reports the name callers expect.
  clike::Builder builder(*this);
  return builder.GetBuiltinType(
      "__size_t", m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size,
      lldb::eEncodingUint, lldb::eFormatUnsigned);
}

CompilerType TypeSystemClike::GetPointerDiffType(bool is_signed) {
  // The result of pointer subtraction. Clang spells this builtin `__ptrdiff_t`
  // (and its unsigned counterpart), sized as a pointer. Model it as a bespoke
  // builtin of pointer width so value objects created from it show that name.
  const uint64_t byte_size =
      m_context.GetLanguageOpts().GetBuiltinSizes().pointer_size;
  clike::Builder builder(*this);
  if (is_signed)
    return builder.GetBuiltinType("__ptrdiff_t", byte_size, lldb::eEncodingSint,
                                  lldb::eFormatDecimal);
  return builder.GetBuiltinType("__ptrdiff_t unsigned", byte_size,
                                lldb::eEncodingUint, lldb::eFormatUnsigned);
}

unsigned TypeSystemClike::GetPtrAuthKey(lldb::opaque_compiler_type_t type) {
  return 0;
}

unsigned
TypeSystemClike::GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t type) {
  return 0;
}

bool TypeSystemClike::GetPtrAuthAddressDiversity(
    lldb::opaque_compiler_type_t type) {
  return false;
}

ConstString TypeSystemClike::GetTypeName(opaque_compiler_type_t type,
                                         bool BaseOnly) {
  LockedType t = GetTypeForWrite(type);
  return GetTypeNameAssumingWriteLocked(t.get(), BaseOnly);
}

ConstString TypeSystemClike::GetTypeNameAssumingWriteLocked(clike::Type *t,
                                                            bool BaseOnly) {
  if (!t)
    return ConstString();
  return ConstString(clike::BuildCanonicalName(t, BaseOnly));
}

ConstString TypeSystemClike::GetDisplayTypeName(opaque_compiler_type_t type) {
  LockedType t = GetTypeForWrite(type);
  return GetDisplayTypeNameAssumingWriteLocked(t.get());
}

ConstString
TypeSystemClike::GetDisplayTypeNameAssumingWriteLocked(clike::Type *t) {
  if (!t)
    return ConstString();
  return ConstString(clike::BuildDisplayName(t));
}

uint32_t
TypeSystemClike::GetTypeInfo(opaque_compiler_type_t type,
                             CompilerType *pointee_or_element_compiler_type) {
  if (pointee_or_element_compiler_type)
    pointee_or_element_compiler_type->Clear();
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return 0;
  const clike::Type *t = tt.get();
  return t->GetTypeInfo();
}

LanguageType TypeSystemClike::GetMinimumLanguage(opaque_compiler_type_t type) {
  // TypeSystemClike models C/C++/Objective-C++ types. Records report C++,
  // which is what puts them in the C++ formatter category, so that e.g. the
  // libc++ container data formatters are consulted (see
  // FormatManager::GetCandidateLanguages); see clike::Type::GetMinimumLanguage
  // for the rest.
  SharedLockedType tt = GetTypeForRead(type);
  if (tt)
    return Desugar(tt.get())->GetMinimumLanguage();
  return eLanguageTypeC_plus_plus;
}

TypeClass TypeSystemClike::GetTypeClass(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t ? t->GetTypeClass() : eTypeClassInvalid;
}

CompilerType
TypeSystemClike::GetArrayElementType(lldb::opaque_compiler_type_t type,
                                     ExecutionContextScope *exe_scope) {
  return CompilerType();
}

CompilerType TypeSystemClike::GetCanonicalType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  // The canonical type is the type with all typedef/cv sugar stripped.
  return GetCompilerType(Desugar(t.get()));
}

CompilerType
TypeSystemClike::GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

int TypeSystemClike::GetFunctionArgumentCount(
    lldb::opaque_compiler_type_t type) {
  return 0;
}

CompilerType TypeSystemClike::GetFunctionArgumentTypeAtIndex(
    lldb::opaque_compiler_type_t type, size_t idx) {
  return CompilerType();
}

CompilerType
TypeSystemClike::GetFunctionReturnType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

size_t TypeSystemClike::GetNumMemberFunctions(opaque_compiler_type_t type) {
  // C records have no member functions.
  return 0;
}

TypeMemberFunctionImpl
TypeSystemClike::GetMemberFunctionAtIndex(opaque_compiler_type_t type,
                                          size_t idx) {
  return TypeMemberFunctionImpl();
}

CompilerType
TypeSystemClike::GetPointeeType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType
TypeSystemClike::GetPointerType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

const llvm::fltSemantics &
TypeSystemClike::GetFloatTypeSemantics(size_t byte_size, Format format) {
  return m_context.GetLanguageOpts().GetFloatTypeSemantics(byte_size, format);
}

llvm::Expected<uint64_t>
TypeSystemClike::GetBitSize(opaque_compiler_type_t type,
                            ExecutionContextScope *exe_scope) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return llvm::createStringError("invalid type");
  const clike::Type *t = Desugar(tt.get());
  if (std::optional<uint64_t> byte_size = t->GetByteSize())
    return *byte_size * 8;
  return llvm::createStringError("TypeSystemClike::GetBitSize: unknown size");
}

Encoding TypeSystemClike::GetEncoding(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t ? t->GetEncoding() : eEncodingInvalid;
}

Format TypeSystemClike::GetFormat(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t ? t->GetFormat() : eFormatDefault;
}

llvm::Expected<uint32_t>
TypeSystemClike::GetNumChildren(lldb::opaque_compiler_type_t type,
                                bool omit_empty_base_classes,
                                const ExecutionContext *exe_ctx) {
  return 0;
}

BasicType
TypeSystemClike::GetBasicTypeEnumeration(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return eBasicTypeInvalid;
  return Desugar(t.get())->GetBasicTypeEnumeration();
}

bool TypeSystemClike::IsPromotableIntegerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && IsPromotableIntegerTypeImpl(t.get());
}

bool TypeSystemClike::IsPromotableIntegerTypeImpl(const clike::Type *t) {
  // Follows C++ [conv.prom]: integer types with a conversion rank less than
  // int, plus bool/character/unscoped-enum types, promote to int/unsigned int.
  return Desugar(t)->IsPromotableInteger();
}

CompilerType
TypeSystemClike::GetPromotedIntegerType(opaque_compiler_type_t type) {
  // May allocate a fresh builtin type via GetBasicTypeFromAST below, so this
  // needs the write lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt || !IsPromotableIntegerTypeImpl(tt.get()))
    return CompilerType();

  CompilerType int_type = GetBasicTypeFromASTAssumingWriteLocked(eBasicTypeInt);
  std::optional<uint64_t> int_size =
      GetClikeType(int_type.GetOpaqueQualType())->GetByteSize();

  // The result of integer promotion is `int` if int can represent all values
  // of the source type, otherwise `unsigned int`. Compare byte sizes and
  // signedness against int.
  std::optional<uint64_t> src_size = tt.get()->GetByteSize();

  bool is_signed = false;
  bool src_is_integer = IsIntegerTypeImpl(tt.get(), is_signed);

  if (src_size && int_size && *src_size >= *int_size && src_is_integer &&
      !is_signed) {
    // An unsigned source that is at least as wide as int cannot be represented
    // by int; promote to unsigned int instead.
    return GetBasicTypeFromASTAssumingWriteLocked(eBasicTypeUnsignedInt);
  }
  return int_type;
}

uint32_t TypeSystemClike::GetNumFields(lldb::opaque_compiler_type_t type) {
  return 0;
}

CompilerType TypeSystemClike::GetFieldAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx, std::string &name,
                                              uint64_t *bit_offset_ptr,
                                              uint32_t *bitfield_bit_size_ptr,
                                              bool *is_bitfield_ptr) {
  return CompilerType();
}

uint32_t TypeSystemClike::GetNumDirectBaseClasses(opaque_compiler_type_t type) {
  // C records have no base classes.
  return 0;
}

uint32_t
TypeSystemClike::GetNumVirtualBaseClasses(opaque_compiler_type_t type) {
  return 0;
}

CompilerType TypeSystemClike::GetDirectBaseClassAtIndex(
    opaque_compiler_type_t type, size_t idx, uint32_t *bit_offset_ptr) {
  return CompilerType();
}

CompilerType TypeSystemClike::GetVirtualBaseClassAtIndex(
    opaque_compiler_type_t type, size_t idx, uint32_t *bit_offset_ptr) {
  return CompilerType();
}

llvm::Expected<CompilerType> TypeSystemClike::GetDereferencedType(
    lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx,
    std::string &deref_name, uint32_t &deref_byte_size,
    int32_t &deref_byte_offset, ValueObject *valobj, uint64_t &language_flags) {
  return CompilerType();
}

llvm::Expected<CompilerType> TypeSystemClike::GetChildCompilerTypeAtIndex(
    lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {
  return CompilerType();
}

llvm::Expected<uint32_t>
TypeSystemClike::GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                                         llvm::StringRef name,
                                         bool omit_empty_base_classes) {
  return 0;
}

size_t TypeSystemClike::GetIndexOfChildMemberWithName(
    lldb::opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  return 0;
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void TypeSystemClike::dump(opaque_compiler_type_t type) const {
}
#endif

bool TypeSystemClike::DumpTypeValue(opaque_compiler_type_t type, Stream &s,
                                    Format format, const DataExtractor &data,
                                    offset_t data_offset, size_t data_byte_size,
                                    uint32_t bitfield_bit_size,
                                    uint32_t bitfield_bit_offset,
                                    ExecutionContextScope *exe_scope) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return false;
  const clike::Type *t = Desugar(tt.get());
  // Aggregates don't have a scalar value to print; their children are dumped
  // individually.
  if (t->IsAggregate())
    return false;
  if (format == eFormatDefault)
    format = t->GetFormat();
  // Some formats dump the value as a sequence of smaller items rather than one
  // scalar: e.g. the char/bytes formats print each byte, and the unicode
  // formats print each code unit. Split the byte size into that many items so
  // e.g. a 16-byte `__uint128_t` printed with a char format is shown as 16
  // characters instead of being rejected as too wide (matching
  // TypeSystemClang).
  uint32_t item_count = 1;
  switch (format) {
  case eFormatChar:
  case eFormatCharPrintable:
  case eFormatCharArray:
  case eFormatBytes:
  case eFormatUnicode8:
  case eFormatBytesWithASCII:
    item_count = data_byte_size;
    data_byte_size = 1;
    break;
  case eFormatUnicode16:
    item_count = data_byte_size / 2;
    data_byte_size = 2;
    break;
  case eFormatUnicode32:
    item_count = data_byte_size / 4;
    data_byte_size = 4;
    break;
  default:
    break;
  }
  return DumpDataExtractor(data, &s, data_offset, format, data_byte_size,
                           item_count, UINT32_MAX, LLDB_INVALID_ADDRESS,
                           bitfield_bit_size, bitfield_bit_offset, exe_scope);
}

void TypeSystemClike::DumpTypeDescription(opaque_compiler_type_t type,
                                          DescriptionLevel level) {
  StreamFile s(stdout, false);
  DumpTypeDescription(type, s, level);
}

void TypeSystemClike::DumpTypeDescription(opaque_compiler_type_t type,
                                          Stream &s, DescriptionLevel level) {
  // Lazily completes the type below, so this needs the write lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return;
  // A typedef is checked before desugaring (unlike the record/enum branches
  // below, which want the canonical type): clang's equivalent dump
  // only special-cases a type that IS itself a TypedefType, printing
  // "typedef <name>" and leaving the underlying type alone, matching
  // TypeSystemClang::DumpTypeDescription's `case clang::Type::Typedef`.
  if (tt->GetTypedefedType()) {
    s.PutCString("typedef ");
    s.PutCString(GetTypeNameAssumingWriteLocked(tt.get(), /*BaseOnly=*/true)
                     .GetStringRef());
    return;
  }
  clike::Type *t = Desugar(tt.get());

  // Anything else: just print its name.
  if (ConstString name = GetTypeNameAssumingWriteLocked(t, /*BaseOnly=*/false))
    s.PutCString(name.GetStringRef());
}

void TypeSystemClike::Dump(llvm::raw_ostream &output, llvm::StringRef filter,
                           bool show_color) {
  // Collect every record type this system has produced and hand them to the
  // Clang-AST synthesizer (which lives in the expression-parser plugin, since
  // TypeSystem/Clike must not depend on the clang AST) to build and print a
  // throwaway clang AST. Backs `target modules dump ast`.
  // The synthesizer that turns those records into a printable clang AST lives
  // in the expression-parser plugin and arrives with ClangASTGenerator; until
  // then there is nothing to print.
}

bool TypeSystemClike::IsRuntimeGeneratedType(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsPointerOrReferenceType(
    lldb::opaque_compiler_type_t type, CompilerType *pointee_type) {
  return false;
}

unsigned TypeSystemClike::GetTypeQualifiers(lldb::opaque_compiler_type_t type) {
  return 0;
}

std::optional<size_t>
TypeSystemClike::GetTypeBitAlign(opaque_compiler_type_t type,
                                 ExecutionContextScope *exe_scope) {
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return std::nullopt;
  const clike::Type *t = Desugar(tt.get());
  return t ? t->GetAlignmentInBits() : std::nullopt;
}

CompilerType TypeSystemClike::GetBuiltinTypeByName(ConstString name) {
  // The `_BitInt`/`unsigned _BitInt` branch below allocates a bespoke tracked
  // type, so this needs the write lock.
  auto write_lock = LockForWrite();
  llvm::StringRef name_ref = name.GetStringRef();

  // `_BitInt(N)` / `unsigned _BitInt(N)` are synthesized on demand (they never
  // appear as an enumerated builtin spelling). Mirror TypeSystemClang: parse
  // the bit width and lay the type out with the target's ABI size.
  bool bitint_unsigned = name_ref.consume_front("unsigned _BitInt(");
  if (bitint_unsigned || name_ref.consume_front("_BitInt(")) {
    uint64_t bits;
    if (name_ref.consumeInteger(/*Radix=*/10, bits))
      return CompilerType();
    if (name_ref != ")")
      return CompilerType();
    std::optional<uint64_t> byte_size =
        m_context.GetLanguageOpts().GetBitIntByteSize(bits);
    if (!byte_size)
      return CompilerType();
    return GetCompilerType(m_context.GetBuiltinType(
        name.GetStringRef(), *byte_size,
        bitint_unsigned ? lldb::eEncodingUint : lldb::eEncodingSint,
        bitint_unsigned ? lldb::eFormatUnsigned : lldb::eFormatDecimal));
  }

  // `__int128_t` / `__uint128_t` are aliases for the 128-bit integer builtins
  // (whose canonical spellings are `__int128` / `unsigned __int128`). They are
  // not themselves builtin spellings, so map them explicitly.
  if (name_ref == "__int128_t")
    return GetCompilerType(
        m_context.GetBuiltinType(clike::BuiltinKind::Int128));
  if (name_ref == "__uint128_t")
    return GetCompilerType(
        m_context.GetBuiltinType(clike::BuiltinKind::UnsignedInt128));

  if (clike::BuiltinType *bt =
          m_context.GetBuiltinTypeByName(name.GetStringRef()))
    return GetCompilerType(bt);
  return CompilerType();
}

CompilerType TypeSystemClike::GetBasicTypeFromAST(BasicType basic_type) {
  auto write_lock = LockForWrite();
  return GetBasicTypeFromASTAssumingWriteLocked(basic_type);
}

CompilerType
TypeSystemClike::GetBasicTypeFromASTAssumingWriteLocked(BasicType basic_type) {
  // Everything the type system models is one of the enumerated builtins.
  // Anything it doesn't (e.g. eBasicTypeHalf) has no basic type here.
  std::optional<clike::BuiltinKind> kind =
      clike::KnownBuiltinTypes::KindForBasicType(basic_type);
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
}

CompilerType
TypeSystemClike::GetBuiltinTypeForEncodingAndBitSize(Encoding encoding,
                                                     size_t bit_size) {
  auto read_lock = LockForRead();
  std::optional<clike::BuiltinKind> kind =
      clike::KnownBuiltinTypes::KindForEncodingAndBitSize(encoding, bit_size);
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
}

bool TypeSystemClike::IsBeingDefined(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsConst(lldb::opaque_compiler_type_t type) {
  return false;
}

uint32_t
TypeSystemClike::IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                        CompilerType *base_type_ptr) {
  return 0;
}

bool TypeSystemClike::IsPolymorphicClass(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsTypedefType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && t->StripElaborated()->GetTypedefedType();
}

CompilerType TypeSystemClike::GetTypedefedType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  return GetCompilerType(t->StripElaborated()->GetTypedefedType());
}

bool TypeSystemClike::IsVectorType(lldb::opaque_compiler_type_t type,
                                   CompilerType *element_type, uint64_t *size) {
  return false;
}

CompilerType
TypeSystemClike::GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) {
  return CompilerType();
}

CompilerType TypeSystemClike::GetNonReferenceType(opaque_compiler_type_t type) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return CompilerType();
  // A reference may hide behind sugar (e.g. `typedef int &td_int_ref`), so look
  // through the sugar to find it -- matching IsReferenceType, which also
  // Desugars. If they disagreed (IsReferenceType true but this returning the
  // sugared type unchanged) a consumer that loops while IsReferenceType() holds
  // -- like FormatManager::GetPossibleMatches -- would recurse forever on a
  // typedef-of-reference. Once the reference is found, peel only the reference
  // (not the referent's own typedef/cv sugar), mirroring clang: the
  // non-reference type of `const int &` is `const int`, and of `td_int_ref` is
  // `int`.
  const clike::Type *t = Desugar(tt.get());
  if (t->IsReference())
    return GetCompilerType(t->GetPointeeType());
  return CompilerType(weak_from_this(), type);
}

bool TypeSystemClike::IsReferenceType(opaque_compiler_type_t type,
                                      CompilerType *pointee_type,
                                      bool *is_rvalue) {
  if (pointee_type)
    pointee_type->Clear();
  if (is_rvalue)
    *is_rvalue = false;
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return false;
  const clike::Type *ref = Desugar(tt.get());
  if (!ref->IsReference())
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(ref->GetPointeeType());
  if (is_rvalue)
    *is_rvalue = ref->IsRValueReference();
  return true;
}
