//===-- TypeSystemClike.cpp -----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "TypeSystemClike.h"

#include "lldb/Core/Module.h"
#include "lldb/Symbol/CompileUnit.h"
#include "llvm/ADT/DenseSet.h"

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

  // Fold in every scratch instance's set. A scratch locks all of its target's
  // modules -- it holds types built around theirs, and reaches into them by
  // control flow alone on the Objective-C runtime-completion path -- so a
  // module one of them covers has to lock the same instances the scratch would.
  // If it locked only itself, that runtime-completion path would then reach
  // into the scratch and take its locks in the opposite order.
  //
  // Pulled from the registry rather than pushed by the scratch, so that only a
  // module ever holds two instances' m_lock_order_mutex and there is no cycle
  // between them. For the same reason a scratch does *not* fold in the other
  // scratches: they belong to unrelated targets, and asking each other for a
  // lock order would recurse between their m_lock_order_mutex and hang.
  llvm::SmallVector<TypeSystemClike *, 2> scratches = GetScratchInstances();
  if (!llvm::is_contained(scratches, this))
    for (TypeSystemClike *scratch : scratches)
      for (TypeSystemClike *ts : scratch->GetLockOrder())
        out.push_back(ts);

  // The Clang modules (`-gmodules` .pcm files) this instance's debug info
  // imports. Each is a separate lldb Module with its own TypeSystemClike, and a
  // record here can name a field, base class, typedef target or Objective-C
  // superclass that one of them owns. The import list is recorded in the DWARF,
  // so it is available without parsing any type -- which is what lets the set
  // be complete before the first query, rather than only after one has already
  // followed such a reference.
  SymbolFile *sym_file = GetSymbolFile();
  if (!sym_file)
    return;
  llvm::DenseSet<SymbolFile *> visited;
  const uint32_t num_cus = sym_file->GetNumCompileUnits();
  for (uint32_t i = 0; i < num_cus; ++i) {
    CompUnitSP cu = sym_file->GetCompileUnitAtIndex(i);
    if (!cu)
      continue;
    // Walks transitively (a module imported by an imported module is visited
    // too) and skips symbol files it has already seen.
    cu->ForEachExternalModule(visited, [&out](Module &module) {
      AppendOrCreateClikeTypeSystemOf(module, out);
      return false; // Keep going; we want all of them, not the first.
    });
  }
}

void TypeSystemClike::AppendClikeTypeSystemsOf(
    Module &module, llvm::SmallVectorImpl<TypeSystemClike *> &out) {
  // Deliberately only picks up type systems that already exist rather than
  // asking for one by language: creating a TypeSystemClike for every module a
  // query might touch would be a large, mostly wasted cost, and a module that
  // has never been asked for a type has none of ours to lock.
  module.ForEachTypeSystem([&out](lldb::TypeSystemSP ts_sp) {
    if (auto *clike = llvm::dyn_cast_or_null<TypeSystemClike>(ts_sp.get()))
      out.push_back(clike);
    return true; // Keep iterating.
  });
}

/// The live scratch instances. A handful at most (one per target), so a plain
/// vector under a mutex is enough.
static std::mutex &GetScratchRegistryMutex() {
  static std::mutex mutex;
  return mutex;
}
static llvm::SmallVector<TypeSystemClike *, 2> &GetScratchRegistry() {
  static llvm::SmallVector<TypeSystemClike *, 2> registry;
  return registry;
}

void TypeSystemClike::RegisterScratchInstance(TypeSystemClike *scratch) {
  std::lock_guard<std::mutex> guard(GetScratchRegistryMutex());
  GetScratchRegistry().push_back(scratch);
}

void TypeSystemClike::UnregisterScratchInstance(TypeSystemClike *scratch) {
  std::lock_guard<std::mutex> guard(GetScratchRegistryMutex());
  llvm::erase(GetScratchRegistry(), scratch);
}

llvm::SmallVector<TypeSystemClike *, 2> TypeSystemClike::GetScratchInstances() {
  std::lock_guard<std::mutex> guard(GetScratchRegistryMutex());
  return GetScratchRegistry();
}

void TypeSystemClike::AppendOrCreateClikeTypeSystemOf(
    Module &module, llvm::SmallVectorImpl<TypeSystemClike *> &out) {
  const size_t before = out.size();
  AppendClikeTypeSystemsOf(module, out);
  if (out.size() != before)
    return;
  // TypeSystemClang::CreateInstance is what hands out a TypeSystemClike for a
  // module when the setting is on, keyed on the C++ language entry.
  llvm::Expected<lldb::TypeSystemSP> ts_or =
      module.GetTypeSystemForLanguage(lldb::eLanguageTypeC_plus_plus);
  if (!ts_or) {
    llvm::consumeError(ts_or.takeError());
    return;
  }
  if (auto *clike = llvm::dyn_cast_or_null<TypeSystemClike>(ts_or->get()))
    out.push_back(clike);
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
  // A TypeSystemClike CompilerDeclContext wraps a clike::Namespace (the
  // global namespace is a null opaque pointer, which is never a valid
  // CompilerDeclContext).
  if (!opaque_decl_ctx)
    return ConstString();
  auto read_lock = LockForRead();
  auto *ns = static_cast<const clike::Namespace *>(opaque_decl_ctx);
  return ConstString(ns->GetName().GetName());
}

ConstString
TypeSystemClike::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  if (!opaque_decl_ctx)
    return ConstString();
  auto read_lock = LockForRead();
  auto *ns = static_cast<const clike::Namespace *>(opaque_decl_ctx);
  // Build "A::B::C" from the namespace chain, skipping the (transparent)
  // inline namespaces so the spelling matches the source.
  llvm::SmallVector<llvm::StringRef, 4> parts;
  for (const clike::Namespace *cur = ns; cur; cur = cur->GetParent()) {
    if (cur->IsInline())
      continue;
    parts.push_back(cur->GetName().GetName());
  }
  std::string qualified;
  for (llvm::StringRef part : llvm::reverse(parts)) {
    if (!qualified.empty())
      qualified += "::";
    qualified += part.str();
  }
  return ConstString(qualified);
}

bool TypeSystemClike::DeclContextIsClassMethod(void *opaque_decl_ctx) {
  return false;
}

std::vector<lldb_private::CompilerContext>
TypeSystemClike::DeclContextGetCompilerContext(void *opaque_decl_ctx) {
  // Build the CompilerContext chain (topmost namespace first) so a
  // namespace-scoped type query (TypeQuery(decl_ctx, name)) can match a type's
  // DWARF lookup context. Inline namespaces stay in the chain (they are
  // transparent for name printing but the DWARF context lists them too);
  // ContextMatches handles anonymous namespaces optionally.
  auto read_lock = LockForRead();
  std::vector<lldb_private::CompilerContext> context;
  for (auto *ns = static_cast<const clike::Namespace *>(opaque_decl_ctx); ns;
       ns = ns->GetParent())
    context.push_back(
        {CompilerContextKind::Namespace, ConstString(ns->GetName().GetName())});
  std::reverse(context.begin(), context.end());
  return context;
}

bool TypeSystemClike::DeclContextIsContainedInLookup(
    void *opaque_decl_ctx, void *other_opaque_decl_ctx) {
  // Namespaces are interned uniquely per Context, so identity is pointer
  // equality. The lookup of a namespace also transparently contains any inline
  // namespace nested (transitively through inline namespaces) inside it, so
  // walk `other` up through its inline-namespace parents looking for a match.
  auto read_lock = LockForRead();
  auto *self = static_cast<const clike::Namespace *>(opaque_decl_ctx);
  auto *other = static_cast<const clike::Namespace *>(other_opaque_decl_ctx);
  for (const clike::Namespace *cur = other; cur; cur = cur->GetParent()) {
    if (cur == self)
      return true;
    // Keep ascending while we are inside a transparent namespace: members of an
    // inline namespace (and of an unnamed namespace, which is implicitly a
    // using-directive into its parent) are visible in the enclosing scope. A
    // named, non-inline namespace is a distinct lookup scope, so stop there.
    if (!cur->IsInline() && !cur->GetName().GetName().empty())
      break;
  }
  return false;
}

LanguageType TypeSystemClike::DeclContextGetLanguage(void *opaque_decl_ctx) {
  return eLanguageTypeC_plus_plus;
}

#ifndef NDEBUG
bool TypeSystemClike::Verify(opaque_compiler_type_t type) { return true; }
#endif

bool TypeSystemClike::IsArrayType(opaque_compiler_type_t type,
                                  CompilerType *element_type, uint64_t *size,
                                  bool *is_incomplete) {
  return IsArrayTypeImpl(GetTypeForRead(type).get(), element_type, size,
                         is_incomplete);
}

bool TypeSystemClike::IsArrayTypeImpl(const clike::Type *type,
                                      CompilerType *element_type,
                                      uint64_t *size, bool *is_incomplete) {
  if (element_type)
    element_type->Clear();
  if (size)
    *size = 0;
  if (is_incomplete)
    *is_incomplete = false;

  const clike::Type *t = Desugar(type);
  clike::Type *element = t ? t->GetArrayElementType() : nullptr;
  if (!element)
    return false;
  // A vector type (DW_AT_GNU_vector) is laid out like an array but is not an
  // array type (matching TypeSystemClang, where a Vector/ExtVector is distinct
  // from ConstantArray). Reporting it as an array makes char-element vectors
  // match the char-array-to-string formatter and mis-render.
  if (t->IsVector())
    return false;

  if (element_type)
    *element_type = GetCompilerType(element);
  if (std::optional<uint64_t> num_elements = t->GetNumElements()) {
    if (size)
      *size = *num_elements;
  } else if (is_incomplete) {
    *is_incomplete = true;
  }
  return true;
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

bool TypeSystemClike::IsFunctionType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && Desugar(t.get())->IsFunction();
}

size_t
TypeSystemClike::GetNumberOfFunctionArguments(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return 0;
  return Desugar(t.get())->GetNumParameters();
}

CompilerType
TypeSystemClike::GetFunctionArgumentAtIndex(opaque_compiler_type_t type,
                                            const size_t index) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  return GetCompilerType(Desugar(t.get())->GetParameterAtIndex(index));
}

bool TypeSystemClike::IsFunctionPointerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  return Desugar(t.get())->IsFunctionPointer();
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
    opaque_compiler_type_t type, CompilerType *function_pointer_type_ptr) {
  // May allocate a fresh pointer type below, so this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return false;
  clike::Type *ptr = Desugar(t.get());
  if (!ptr->IsBlockPointer())
    return false;
  // Report the corresponding function-pointer type (a plain pointer to the
  // block's function type), mirroring TypeSystemClang.
  if (function_pointer_type_ptr)
    *function_pointer_type_ptr = clike::Builder(*this).CreatePointerType(
        GetCompilerType(ptr->GetPointeeType()));
  return true;
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

bool TypeSystemClike::IsScopedEnumerationType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  return Desugar(t.get())->IsScopedEnumeration();
}

bool TypeSystemClike::IsEnumerationType(opaque_compiler_type_t type,
                                        bool &is_signed) {
  is_signed = false;
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  clike::Type *integer_type = Desugar(t.get())->GetEnumerationIntegerType();
  if (!integer_type)
    return false;
  is_signed = integer_type->GetEncoding() == eEncodingSint;
  return true;
}

bool TypeSystemClike::IsPossibleDynamicType(opaque_compiler_type_t type,
                                            CompilerType *target_type,
                                            bool check_cplusplus,
                                            bool check_objc) {
  if (target_type)
    target_type->Clear();
  // May force-complete a pointee record below, so this needs the write lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return false;

  // A pointer to an Objective-C object, a pointer/reference to a polymorphic
  // C++ class, or a `void *` -- see clike::Type::GetDynamicTypeCandidate. A
  // pointee record may need completing to tell whether it is polymorphic.
  clike::Type *pointee = Desugar(tt.get())->GetDynamicTypeCandidate(
      (check_cplusplus ? clike::DynamicTypeCheck::CPlusPlus
                       : clike::DynamicTypeCheck::None) |
          (check_objc ? clike::DynamicTypeCheck::ObjC
                      : clike::DynamicTypeCheck::None),
      [this](clike::Type *t) { CompleteTypeAssumingWriteLocked(t); });
  if (!pointee)
    return false;
  if (target_type)
    target_type->SetCompilerType(weak_from_this(),
                                 static_cast<opaque_compiler_type_t>(pointee));
  return true;
}

bool TypeSystemClike::IsPointerType(opaque_compiler_type_t type,
                                    CompilerType *pointee_type) {
  if (pointee_type)
    pointee_type->Clear();
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return false;
  const clike::Type *t = Desugar(tt.get());
  if (!t->IsPointer())
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(t->GetPointeeType());
  return true;
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

bool TypeSystemClike::IsForcefullyCompleted(opaque_compiler_type_t type) {
  if (!type)
    return false;
  LockedType t = GetTypeForWrite(type);
  // TypeSystemClike never force-completes a record as an empty definition the
  // way TypeSystemClang does (see GetCompleteType above) -- a record with no
  // definition anywhere in the debug info is left genuinely incomplete
  // instead. So this reports the closest equivalent: a record/enum that is
  // still incomplete after best-effort completion (i.e. -flimit-debug-info
  // stripped its only definition). ValueObject::GetSummaryAsCString uses this
  // to print "<incomplete type>" instead of trying to summarize a type it has
  // no members for. Scoped to RecordType (not desugared through references
  // etc.) to mirror TypeSystemClang::IsForcefullyCompleted, which only
  // recognizes a plain clang::RecordType.
  clike::Type *record = Desugar(t.get());
  if (!record->IsRecord())
    return false;
  clike::Type *completed = CompleteTypeAssumingWriteLocked(record);
  return !(completed && completed->IsComplete());
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

unsigned TypeSystemClike::GetPtrAuthKey(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (t)
    if (std::optional<clike::PtrAuthQualifier> q = t->GetPtrAuthQualifier())
      return q->GetKey();
  return 0;
}

unsigned TypeSystemClike::GetPtrAuthDiscriminator(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (t)
    if (std::optional<clike::PtrAuthQualifier> q = t->GetPtrAuthQualifier())
      return q->GetExtraDiscriminator();
  return 0;
}

bool TypeSystemClike::GetPtrAuthAddressDiversity(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (t)
    if (std::optional<clike::PtrAuthQualifier> q = t->GetPtrAuthQualifier())
      return q->IsAddressDiscriminated();
  return false;
}

bool TypeSystemClike::HasPointerAuthQualifier(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && t->GetPtrAuthQualifier().has_value();
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
  // Hand back the element/pointee type when asked; callers such as
  // ValueObject::GetPointeeData rely on it to know how to read array elements
  // or dereference pointers/references.
  if (pointee_or_element_compiler_type) {
    const clike::Type *desugared = Desugar(t);
    clike::Type *inner = desugared->GetArrayElementType();
    if (!inner)
      inner = desugared->GetPointeeType();
    if (inner)
      *pointee_or_element_compiler_type = GetCompilerType(inner);
  }
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
TypeSystemClike::GetArrayElementType(opaque_compiler_type_t type,
                                     ExecutionContextScope *exe_scope) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  return GetCompilerType(Desugar(t.get())->GetArrayElementType());
}

CompilerType TypeSystemClike::GetArrayType(opaque_compiler_type_t type,
                                           uint64_t size) {
  // Allocates a fresh ArrayType node.
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  // Build an array of `size` elements of `type` (size 0 => unbounded). Used by
  // the formatter matcher to form a typedef-stripped array candidate name (e.g.
  // `MCHAR[5]` -> `char[5]`), so char-array-of-typedef matches the char[]
  // string summary.
  return clike::Builder(*this).CreateArrayType(
      GetCompilerType(t.get()), size ? std::optional<uint64_t>(size)
                                     : std::optional<uint64_t>(std::nullopt));
}

CompilerType TypeSystemClike::GetCanonicalType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  // The canonical type is the type with all typedef/cv sugar stripped.
  return GetCompilerType(Desugar(t.get()));
}

CompilerType
TypeSystemClike::GetEnumerationIntegerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  return GetCompilerType(Desugar(t.get())->GetEnumerationIntegerType());
}

void TypeSystemClike::ForEachEnumerator(
    opaque_compiler_type_t type,
    std::function<bool(const CompilerType &integer_type, ConstString name,
                       const llvm::APSInt &value)> const &callback) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return;
  const clike::Type *enum_type = Desugar(t.get());
  clike::Type *underlying = enum_type->GetEnumerationIntegerType();
  if (!underlying)
    return;

  CompilerType integer_type = GetCompilerType(underlying);
  const bool is_signed = underlying->GetEncoding() == eEncodingSint;
  // Enumerator values are stored as raw bits; recover their APSInt using the
  // underlying type's width and signedness so that values are extended
  // correctly. Read the
  // byte size directly off the underlying Type (not via
  // integer_type.GetByteSize(), which would call back into this instance's
  // own GetBitSize and re-acquire the lock already held above).
  unsigned bit_width = 64;
  if (std::optional<uint64_t> byte_size = underlying->GetByteSize())
    bit_width = static_cast<unsigned>(*byte_size * 8);

  for (const clike::Enumerator &enumerator : enum_type->GetEnumerators()) {
    llvm::APSInt value =
        llvm::APSInt(enumerator.GetValue(), !is_signed).extOrTrunc(bit_width);
    if (!callback(integer_type, ConstString(enumerator.GetName().GetName()),
                  value))
      break;
  }
}

int TypeSystemClike::GetFunctionArgumentCount(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return -1;
  const clike::Type *fn = Desugar(t.get());
  if (!fn->IsFunction())
    return -1;
  return static_cast<int>(fn->GetNumParameters());
}

CompilerType
TypeSystemClike::GetFunctionArgumentTypeAtIndex(opaque_compiler_type_t type,
                                                size_t idx) {
  return GetFunctionArgumentAtIndex(type, idx);
}

CompilerType
TypeSystemClike::GetFunctionReturnType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  return GetCompilerType(Desugar(t.get())->GetReturnType());
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

CompilerType TypeSystemClike::GetPointeeType(opaque_compiler_type_t type) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return CompilerType();
  // A `void *` points at the `void` builtin, so this is a valid CompilerType
  // for every pointer -- which is what callers like
  // CompilerType::IsPointerToVoid expect (and what TypeSystemClang, whose
  // `void` QualType is always valid, does). A reference's "pointee" is the
  // referenced type, matching TypeSystemClang::GetPointeeType
  // (clang::Type::getPointeeType() answers both pointers and references).
  return GetCompilerType(Desugar(tt.get())->GetPointeeType());
}

CompilerType TypeSystemClike::GetPointerType(opaque_compiler_type_t type) {
  // Allocates a fresh PointerType node.
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  return clike::Builder(*this).CreatePointerType(GetCompilerType(t.get()));
}

CompilerType
TypeSystemClike::GetTypeForFormatters(opaque_compiler_type_t type) {
  // May allocate a fresh Pointer/ArrayType below, so this needs the write
  // lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return CompilerType();
  // Data formatters match on type name and are meant to be cv-agnostic (e.g.
  // the C-string summary is registered for `char *`, and clang strips the
  // cv-qualifiers so `const char *` matches too). Peel any top-level
  // cv-qualifier sugar, and -- because a `char *` summary keys off the pointer
  // type's spelling -- also rebuild a pointer whose pointee is cv-qualified as
  // a pointer to the unqualified pointee. Typedefs are preserved (they are
  // meaningful to formatters).
  clike::Type *t = tt->StripCVQualifiers();

  if (t->IsPointer()) {
    clike::Type *pointee = t->GetPointeeType();
    clike::Type *stripped = pointee->StripCVQualifiers();
    if (stripped != pointee)
      return clike::Builder(*this).CreatePointerType(GetCompilerType(stripped));
  }
  // The C-string array summary keys off an array type's spelling
  // (`char[N]` / `unsigned char[N]`), which clang produces by stripping the
  // element's cv-qualifiers. An array of `const char` is modeled here with the
  // cv-qualifier on the element (not the array), so rebuild the array with the
  // unqualified element so `const char[N]` also matches.
  if (clike::Type *element = t->GetArrayElementType()) {
    clike::Type *stripped = element->StripCVQualifiers();
    if (stripped != element)
      return clike::Builder(*this).CreateArrayType(GetCompilerType(stripped),
                                                   t->GetNumElements());
  }
  return GetCompilerType(t);
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
  // Function types have no storage of their own. Matching TypeSystemClang
  // (clang models function types with a type size of 0), report a bit size of
  // 0 rather than an error. This keeps the dereferenced-value child of a
  // function pointer/reference from surfacing a size error (or a spurious byte
  // read) as its summary -- a zero-sized value simply has no value string.
  if (t->IsFunction())
    return 0;
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
TypeSystemClike::GetNumChildren(opaque_compiler_type_t type,
                                bool omit_empty_base_classes,
                                const ExecutionContext *exe_ctx) {
  // Lazily completes the type (and recurses through transparent
  // pointers/references), so this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  return GetNumChildrenImpl(t.get(), omit_empty_base_classes, exe_ctx);
}

llvm::Expected<uint32_t>
TypeSystemClike::GetNumChildrenImpl(clike::Type *type,
                                    bool omit_empty_base_classes,
                                    const ExecutionContext *exe_ctx) {
  if (!type)
    return 0;
  clike::Type *t = Desugar(type);
  // An array's children are its elements.
  if (t->GetArrayElementType()) {
    if (std::optional<uint64_t> n = t->GetNumElements())
      return *n;
    // No static bound: this may be a variable-length array whose length is only
    // known at runtime. Ask the symbol file to resolve it for this frame.
    if (exe_ctx && t->GetDIEUID() != LLDB_INVALID_UID)
      if (SymbolFile *sym_file = GetSymbolFile())
        if (std::optional<SymbolFile::ArrayInfo> info =
                sym_file->GetDynamicArrayInfoForUID(t->GetDIEUID(), exe_ctx))
          if (!info->element_orders.empty())
            return info->element_orders.back().value_or(0);
    return 0;
  }
  // A pointer/reference is transparent when its children are the pointee's
  // members (see Type::GetTransparentChildPointee); otherwise its single child
  // is the dereferenced value -- except for `void *`, which has no pointee to
  // show and therefore no children (a reference can't be to void).
  if (clike::Type *pointee = t->GetPointeeType()) {
    if (clike::Type *transparent = t->GetTransparentChildPointee())
      return GetNumChildrenImpl(transparent, omit_empty_base_classes, exe_ctx);
    return clike::IsVoid(pointee) ? 0 : 1;
  }
  if (!t->IsAggregate())
    return 0;
  CompleteTypeAssumingWriteLocked(type);
  // A record/enum with no debug info defining it anywhere (e.g.
  // -flimit-debug-info hid its only definition, or a forward-declared
  // `struct Opaque;` that is never defined at all) stays incomplete even
  // after GetCompleteType, since TypeSystemClike deliberately does not
  // force-complete it as an empty definition the way TypeSystemClang does
  // (see GetCompleteType's comment). TypeSystemClang's completion source
  // always leaves a Record reporting *some* field count (0, if it could only
  // force-complete an empty definition) rather than erroring at this level --
  // GetCompleteRecordType never returns null for a Record, so its
  // GetNumChildren Record case practically never takes its "incomplete" branch.
  // Match that by reporting zero children (not an error) here; a
  // pointer/reference to such a type still surfaces the error through
  // GetChildCompilerTypeAtIndex's pointer branch instead (see
  // TestValueObjectErrors), which is the scenario the error string was actually
  // meant for.
  if (!t->IsComplete())
    return 0;
  // The children of a record are its fields.
  return t->GetNumFields();
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

  // Unscoped enumerations without a fixed underlying type promote to the first
  // of {int, unsigned int, long, ...} that can represent all enumerator values.
  // For the common case where the values fit in int, that is int -- regardless
  // of whether the DWARF underlying integer happens to be unsigned. Match
  // Clang, which computes the promotion type from the value range.
  const clike::Type *enum_type = Desugar(tt.get());
  if (clike::Type *enum_integer_type = enum_type->GetEnumerationIntegerType()) {
    bool needs_unsigned = false;
    if (int_size) {
      const unsigned int_bits = *int_size * 8;
      const bool enum_signed =
          enum_integer_type->GetEncoding() == eEncodingSint;
      for (const clike::Enumerator &e : enum_type->GetEnumerators()) {
        // int can represent [-2^(n-1), 2^(n-1) - 1].
        const llvm::APInt &v = e.GetValue();
        if (enum_signed ? !v.isSignedIntN(int_bits)
                        : !v.isIntN(int_bits - 1)) {
          needs_unsigned = true;
          break;
        }
      }
    }
    if (needs_unsigned)
      return GetBasicTypeFromASTAssumingWriteLocked(eBasicTypeUnsignedInt);
    return int_type;
  }

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

uint32_t TypeSystemClike::GetNumFields(opaque_compiler_type_t type) {
  // Lazily completes the type, so this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return 0;
  CompleteTypeAssumingWriteLocked(t.get());
  return Desugar(t.get())->GetNumFields();
}

CompilerType TypeSystemClike::GetFieldAtIndex(opaque_compiler_type_t type,
                                              size_t idx, std::string &name,
                                              uint64_t *bit_offset_ptr,
                                              uint32_t *bitfield_bit_size_ptr,
                                              bool *is_bitfield_ptr) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  CompleteTypeAssumingWriteLocked(t.get());
  const Field *field = Desugar(t.get())->GetFieldAtIndex(idx);
  if (!field)
    return CompilerType();
  name = field->GetName().GetName().str();
  if (bit_offset_ptr) {
    *bit_offset_ptr = field->GetByteOffset() * 8;
    if (field->IsBitfield())
      *bit_offset_ptr += field->GetBitfieldBitOffset();
  }
  if (bitfield_bit_size_ptr)
    *bitfield_bit_size_ptr =
        field->IsBitfield() ? field->GetBitfieldBitSize() : 0;
  if (is_bitfield_ptr)
    *is_bitfield_ptr = field->IsBitfield();
  return GetCompilerType(&field->GetType().Get());
}

uint32_t TypeSystemClike::GetNumDirectBaseClasses(opaque_compiler_type_t type) {
  // C records have no base classes.
  return 0;
}

uint32_t
TypeSystemClike::GetNumVirtualBaseClasses(opaque_compiler_type_t type) {
  return 0;
}

bool TypeSystemClike::IsAnonymousType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  // An anonymous struct/union (an unnamed record embedded as an unnamed member
  // of its parent) is marked as such by the DWARF parser. Look through sugar,
  // matching TypeSystemClang's RemoveWrappingTypes.
  return Desugar(t.get())->IsAnonymousStructOrUnion();
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
    opaque_compiler_type_t type, ExecutionContext *exe_ctx,
    std::string &deref_name, uint32_t &deref_byte_size,
    int32_t &deref_byte_offset, ValueObject *valobj, uint64_t &language_flags) {
  // Only pointers, references and arrays can be dereferenced.
  if (!IsPointerOrReferenceType(type, nullptr) &&
      !IsArrayType(type, nullptr, nullptr, nullptr))
    return llvm::createStringError("not a pointer, reference or array type");

  // A `void *` cannot be dereferenced. Report a specific error (matching
  // TypeSystemClang) rather than falling through to a zero-sized child, which
  // would surface a generic "dereference failed" message.
  if (IsPointerType(type, nullptr) && GetPointeeType(type).IsVoidType())
    return llvm::createStringError("cannot dereference void *");

  // The dereferenced value is child 0. Ask for it non-transparently so a
  // pointer-to-aggregate yields the pointee itself rather than its members.
  uint32_t child_bitfield_bit_size = 0;
  uint32_t child_bitfield_bit_offset = 0;
  bool child_is_base_class = false;
  bool child_is_deref_of_parent = false;
  return GetChildCompilerTypeAtIndex(
      type, exe_ctx, /*idx=*/0, /*transparent_pointers=*/false,
      /*omit_empty_base_classes=*/true, /*ignore_array_bounds=*/false,
      deref_name, deref_byte_size, deref_byte_offset, child_bitfield_bit_size,
      child_bitfield_bit_offset, child_is_base_class, child_is_deref_of_parent,
      valobj, language_flags);
}

llvm::Expected<CompilerType> TypeSystemClike::GetChildCompilerTypeAtIndex(
    opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {
  // Lazily completes the type (and recurses through transparent
  // pointers/references), so this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  return GetChildCompilerTypeAtIndexImpl(
      t.get(), exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
      ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
      child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
      child_is_deref_of_parent, valobj, language_flags);
}

llvm::Expected<CompilerType> TypeSystemClike::GetChildCompilerTypeAtIndexImpl(
    clike::Type *type, ExecutionContext *exe_ctx, size_t idx,
    bool transparent_pointers, bool omit_empty_base_classes,
    bool ignore_array_bounds, std::string &child_name,
    uint32_t &child_byte_size, int32_t &child_byte_offset,
    uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
    bool &child_is_base_class, bool &child_is_deref_of_parent,
    ValueObject *valobj, uint64_t &language_flags) {
  child_name.clear();
  child_byte_size = 0;
  child_byte_offset = 0;
  child_bitfield_bit_size = 0;
  child_bitfield_bit_offset = 0;
  child_is_base_class = false;
  child_is_deref_of_parent = false;
  language_flags = 0;

  if (!type)
    return CompilerType();
  CompleteTypeAssumingWriteLocked(type);
  clike::Type *t = Desugar(type);

  // Array elements: child N is the element at offset N * element_size.
  if (clike::Type *element_type = t->GetArrayElementType()) {
    if (!ignore_array_bounds) {
      std::optional<uint64_t> num_elements = t->GetNumElements();
      if (num_elements && idx >= *num_elements)
        return CompilerType();
    }
    child_name = llvm::formatv("[{0}]", idx).str();
    if (std::optional<uint64_t> byte_size = element_type->GetByteSize()) {
      child_byte_size = *byte_size;
      child_byte_offset = idx * *byte_size;
    }
    return GetCompilerType(element_type);
  }

  // A pointer is transparent, mirroring TypeSystemClang: when asked
  // transparently, expanding it splices in the pointee aggregate's members
  // instead of yielding a single deref child. Only an already-complete
  // aggregate is expanded transparently so that merely inspecting a pointer
  // doesn't force completion of an otherwise-lazy pointee; otherwise child 0 is
  // the dereferenced value (which the DIL `ptr->member` path relies on).
  if (t->IsPointer()) {
    clike::Type *pointee = t->GetPointeeType();
    if (clike::IsVoid(pointee))
      return CompilerType(); // Can't dereference `void *`.

    if (transparent_pointers && t->GetTransparentChildPointee()) {
      bool tmp_child_is_deref_of_parent = false;
      return GetChildCompilerTypeAtIndexImpl(
          pointee, exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
          ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
          child_bitfield_bit_size, child_bitfield_bit_offset,
          child_is_base_class, tmp_child_is_deref_of_parent, valobj,
          language_flags);
    }

    child_is_deref_of_parent = true;
    if (const char *parent_name =
            valobj ? valobj->GetName().GetCString() : nullptr) {
      child_name.assign(1, '*');
      child_name += parent_name;
    }
    if (idx != 0)
      return CompilerType();
    // Dereferencing yields the pointee by value, so it must be complete now
    // (this is the explicit access that is allowed to force completion of an
    // otherwise-lazy pointee).
    CompleteTypeAssumingWriteLocked(pointee);
    std::optional<uint64_t> byte_size = pointee->GetByteSize();
    if (!byte_size)
      return llvm::createStringError(
          "incomplete type \"" +
          GetDisplayTypeNameAssumingWriteLocked(pointee).GetString() + "\"");
    child_byte_size = *byte_size;
    child_byte_offset = 0;
    return GetCompilerType(pointee);
  }

  // The children of a record are its fields.
  const Field *field = t->GetFieldAtIndex(idx);
  if (!field)
    return CompilerType();

  child_name = field->GetName().GetName().str();
  child_byte_offset = field->GetByteOffset();
  if (std::optional<uint64_t> byte_size = field->GetType().Get().GetByteSize())
    child_byte_size = *byte_size;
  if (field->IsBitfield()) {
    child_bitfield_bit_size = field->GetBitfieldBitSize();
    child_bitfield_bit_offset = field->GetBitfieldBitOffset();
  }
  return GetCompilerType(&field->GetType().Get());
}

llvm::Expected<uint32_t>
TypeSystemClike::GetIndexOfChildWithName(opaque_compiler_type_t type,
                                         llvm::StringRef name,
                                         bool omit_empty_base_classes) {
  LockedType t = GetTypeForWrite(type);
  return GetIndexOfChildWithNameImpl(t.get(), name, omit_empty_base_classes);
}

llvm::Expected<uint32_t> TypeSystemClike::GetIndexOfChildWithNameImpl(
    clike::Type *type, llvm::StringRef name, bool omit_empty_base_classes) {
  if (!type)
    return llvm::createStringError("invalid type");
  CompleteTypeAssumingWriteLocked(type);
  clike::Type *t = Desugar(type);
  // A pointer/reference is transparent for named lookups (mirroring
  // TypeSystemClang): its named children are the aggregate pointee's members,
  // so forward the lookup. A pointer to a non-aggregate has only its (unnamed)
  // deref child, so no named member is directly addressable.
  if (t->GetPointeeType()) {
    if (clike::Type *pointee = t->GetNamedMemberPointee())
      return GetIndexOfChildWithNameImpl(pointee, name,
                                         omit_empty_base_classes);
    return llvm::createStringError(
        "TypeSystemClike::GetIndexOfChildWithName: no such child");
  }
  // The children of a record are its fields.
  for (uint32_t i = 0, e = t->GetNumFields(); i < e; ++i) {
    if (t->GetFieldAtIndex(i)->GetName().GetName() == name)
      return i;
  }
  return llvm::createStringError(
      "TypeSystemClike::GetIndexOfChildWithName: no such child");
}

size_t TypeSystemClike::GetIndexOfChildMemberWithName(
    opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  // Lazily completes the type, so this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  return GetIndexOfChildMemberWithNameImpl(
      t.get(), name, omit_empty_base_classes, child_indexes);
}

size_t TypeSystemClike::GetIndexOfChildMemberWithNameImpl(
    clike::Type *type, llvm::StringRef name, bool omit_empty_base_classes,
    std::vector<uint32_t> &child_indexes) {
  if (!type)
    return 0;
  CompleteTypeAssumingWriteLocked(type);
  clike::Type *t = Desugar(type);
  // A pointer forwards the lookup to its pointee (when that has named
  // members), so that e.g. `ptr->member` resolves against the pointed-to
  // record. A pointer is transparent (see GetNumChildren): its children are the
  // pointee aggregate's members directly, with no intervening deref child, so
  // recurse without pushing an index-0 deref step -- keeping the returned
  // indices consistent with the child layout.
  if (t->GetPointeeType()) {
    clike::Type *pointee = t->GetNamedMemberPointee();
    if (!pointee)
      return 0;
    return GetIndexOfChildMemberWithNameImpl(
        pointee, name, omit_empty_base_classes, child_indexes);
  }

  // A matching field is a direct child. An unnamed field is an anonymous
  // union/struct whose members are reached as if they belonged to this record,
  // so recurse into it.
  for (uint32_t i = 0, e = t->GetNumFields(); i < e; ++i) {
    const Field *field = t->GetFieldAtIndex(i);
    llvm::StringRef field_name = field->GetName().GetName();
    if (field_name == name) {
      child_indexes.push_back(i);
      return child_indexes.size();
    }
    if (field_name.empty()) {
      std::vector<uint32_t> save_indices = child_indexes;
      child_indexes.push_back(i);
      if (GetIndexOfChildMemberWithNameImpl(&field->GetType().Get(), name,
                                            omit_empty_base_classes,
                                            child_indexes))
        return child_indexes.size();
      child_indexes = std::move(save_indices);
    }
  }
  return 0;
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void TypeSystemClike::dump(opaque_compiler_type_t type) const {
}
#endif

/// Render an enum value as an enumerator name when it matches one exactly. For
/// a "bitfield/flag" enum (every enumerator is a single bit or a union of
/// previously-seen bits) a combined value is decomposed into `A | B`. Mirrors
/// TypeSystemClang's DumpEnumValue.
static bool DumpEnumValue(const clike::Type &enum_type, Stream &s,
                          const DataExtractor &data, lldb::offset_t byte_offset,
                          size_t byte_size, uint32_t bitfield_bit_offset,
                          uint32_t bitfield_bit_size) {
  lldb::offset_t offset = byte_offset;
  const bool is_signed =
      enum_type.GetEnumerationIntegerType()->GetEncoding() == eEncodingSint;
  const uint64_t enum_svalue =
      is_signed
          ? static_cast<uint64_t>(data.GetMaxS64Bitfield(
                &offset, byte_size, bitfield_bit_size, bitfield_bit_offset))
          : data.GetMaxU64Bitfield(&offset, byte_size, bitfield_bit_size,
                                   bitfield_bit_offset);

  bool can_be_bitfield = true;
  uint64_t covered_bits = 0;
  int num_enumerators = 0;

  // Look for an exact match while applying the bitfield heuristic: an enum is
  // likely a flag set if every enumerator is a single bit or a superset of the
  // bits seen so far.
  llvm::ArrayRef<clike::Enumerator> enumerators = enum_type.GetEnumerators();
  if (enumerators.empty())
    can_be_bitfield = false;
  // The value to print is at most 64 bits wide, so an enumerator that doesn't
  // fit into 64 bits can never match it and doesn't take part in the flag
  // heuristic. Other enumerators are compared by their (sign-extended if
  // signed) 64 bits.
  auto get_raw_bits =
      [is_signed](const clike::Enumerator &e) -> std::optional<uint64_t> {
    const llvm::APInt &v = e.GetValue();
    if (is_signed)
      return v.isSignedIntN(64) ? std::optional<uint64_t>(v.getSExtValue())
                                : std::nullopt;
    return v.isIntN(64) ? std::optional<uint64_t>(v.getZExtValue())
                        : std::nullopt;
  };
  for (const clike::Enumerator &enumerator : enumerators) {
    std::optional<uint64_t> raw_bits = get_raw_bits(enumerator);
    if (!raw_bits) {
      can_be_bitfield = false;
      continue;
    }
    uint64_t val = *raw_bits;
    if (is_signed)
      val = llvm::SignExtend64(val, 8 * byte_size);
    if (llvm::popcount(val) != 1 && (val & ~covered_bits) != 0)
      can_be_bitfield = false;
    covered_bits |= val;
    ++num_enumerators;
    if (val == enum_svalue) {
      s.PutCString(enumerator.GetName().GetName());
      return true;
    }
  }

  // Unsigned values make more sense for flags.
  offset = byte_offset;
  const uint64_t enum_uvalue = data.GetMaxU64Bitfield(
      &offset, byte_size, bitfield_bit_size, bitfield_bit_offset);

  // No exact match and this isn't a flag enum: print the value numerically.
  if (!can_be_bitfield) {
    if (is_signed)
      s.Printf("%" PRIi64, static_cast<int64_t>(enum_svalue));
    else
      s.Printf("%" PRIu64, enum_uvalue);
    return true;
  }

  if (!enum_uvalue) {
    // Flag enum, but the value is 0 and matched no enumerator above.
    s.Printf("0x%" PRIx64, enum_uvalue);
    return true;
  }

  uint64_t remaining_value = enum_uvalue;
  std::vector<std::pair<uint64_t, llvm::StringRef>> values;
  values.reserve(num_enumerators);
  for (const clike::Enumerator &enumerator : enumerators)
    if (uint64_t raw_bits = get_raw_bits(enumerator).value_or(0))
      values.emplace_back(raw_bits, enumerator.GetName().GetName());

  // Sort by descending population count (stably) so that in
  // `enum { A, B, ALL = A|B }` we emit ALL before A/B, and `A | C` keeps the
  // declaration order for equal popcounts.
  llvm::stable_sort(values, [](const auto &a, const auto &b) {
    return llvm::popcount(a.first) > llvm::popcount(b.first);
  });

  for (const auto &val : values) {
    if ((remaining_value & val.first) != val.first)
      continue;
    remaining_value &= ~val.first;
    s.PutCString(val.second);
    if (remaining_value)
      s.PutCString(" | ");
  }

  // Print any bits not covered by an enumerator as hex.
  if (remaining_value)
    s.Printf("0x%" PRIx64, remaining_value);

  return true;
}

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
  // Enumerations: show the enumerator name when possible rather than the raw
  // integer value.
  if (t->GetEnumerationIntegerType()) {
    if (format == eFormatEnum || format == eFormatDefault)
      return DumpEnumValue(*t, s, data, data_offset, data_byte_size,
                           bitfield_bit_offset, bitfield_bit_size);
  }
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

// Append a C declarator ("<type> <name>") for a record member, using array
// declarator syntax (`char padding[0]`) when the member's type is an array so
// the printed definition matches C source form. Assumes the caller (only
// TypeSystemClike::DumpTypeDescription) already holds the write lock.
void TypeSystemClike::AppendMemberDeclAssumingWriteLocked(
    Stream &s, clike::Type *field_type, llvm::StringRef name) {
  std::string dims;
  clike::Type *cur = field_type;
  while (clike::Type *element = cur ? cur->GetArrayElementType() : nullptr) {
    if (std::optional<uint64_t> n = cur->GetNumElements())
      dims += llvm::formatv("[{0}]", *n).str();
    else
      dims += "[]";
    cur = element;
  }
  std::string base = GetTypeNameAssumingWriteLocked(cur, /*BaseOnly=*/false)
                         .GetStringRef()
                         .str();
  s << base;
  if (!name.empty())
    s << " " << name;
  s << dims;
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

  if (clike::Type *enum_integer_type = t->GetEnumerationIntegerType()) {
    CompleteTypeAssumingWriteLocked(t);
    // Scoped enums (`enum class`) print their tag so the definition matches C++
    // source form. The class/struct distinction is not modeled, so scoped enums
    // always use `class` (clang's default spelling).
    const char *scope = t->IsScopedEnumeration() ? " class" : "";
    s.Printf(
        "enum%s %s {\n", scope,
        GetTypeNameAssumingWriteLocked(t, /*BaseOnly=*/false).GetCString());
    const bool is_signed = enum_integer_type->GetEncoding() == eEncodingSint;
    for (const clike::Enumerator &e : t->GetEnumerators()) {
      llvm::SmallString<40> value;
      e.GetValue().toString(value, /*Radix=*/10, is_signed);
      s.Printf("    %s = %s,\n", e.GetName().GetName().str().c_str(),
               value.c_str());
    }
    s.PutCString("}");
    return;
  }

  if (t->IsRecord()) {
    CompleteTypeAssumingWriteLocked(t);
    // The source-spelling keyword (which doesn't affect layout) is tracked via
    // IsClassKeyword().
    const char *tag =
        t->IsUnion() ? "union" : (t->IsClassKeyword() ? "class" : "struct");
    // This dump context matches clang's printer, which uses the bare
    // (unqualified) tag name here, not the fully-qualified name.
    s.Printf("%s %s {\n", tag,
             GetTypeNameAssumingWriteLocked(t, /*BaseOnly=*/true).GetCString());
    for (uint32_t i = 0, e = t->GetNumFields(); i != e; ++i) {
      const clike::Field *field = t->GetFieldAtIndex(i);
      if (!field)
        continue;
      s.PutCString("    ");
      AppendMemberDeclAssumingWriteLocked(s, &field->GetType().Get(),
                                          field->GetName().GetName());
      if (field->IsBitfield())
        s.Printf(" : %u", field->GetBitfieldBitSize());
      s.PutCString(";\n");
    }
    s.PutCString("}");
    return;
  }

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

bool TypeSystemClike::IsPointerOrReferenceType(opaque_compiler_type_t type,
                                               CompilerType *pointee_type) {
  if (IsPointerType(type, pointee_type))
    return true;
  return IsReferenceType(type, pointee_type, /*is_rvalue=*/nullptr);
}

unsigned TypeSystemClike::GetTypeQualifiers(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return 0;
  return t->GetCVRQualifiers();
}

std::optional<size_t>
TypeSystemClike::GetTypeBitAlign(opaque_compiler_type_t type,
                                 ExecutionContextScope *exe_scope) {
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return std::nullopt;
  CompleteForAlignmentAssumingWriteLocked(tt.get());
  const clike::Type *t = Desugar(tt.get());
  return t ? t->GetAlignmentInBits() : std::nullopt;
}

void TypeSystemClike::CompleteForAlignmentAssumingWriteLocked(
    clike::Type *type) {
  clike::Type *t = Desugar(type);
  while (t && t->GetArrayElementType())
    t = Desugar(t->GetArrayElementType());
  if (!t || !t->IsRecord())
    return;
  CompleteTypeAssumingWriteLocked(t);
  for (uint32_t i = 0, e = t->GetNumFields(); i != e; ++i)
    CompleteForAlignmentAssumingWriteLocked(
        &t->GetFieldAtIndex(i)->GetType().Get());
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

CompilerType TypeSystemClike::CreateGenericFunctionPrototype() {
  // An unprototyped `void ()` function type, used by ValueObjectVTable to give
  // a vtable slot that doesn't resolve to a known function a displayable
  // (hex address + description) function-pointer type. Mirrors
  // TypeSystemClang::CreateGenericFunctionPrototype's
  // ast.getFunctionNoProtoType(ast.VoidTy, ...): a variadic function with no
  // declared parameters is the closest match to "no prototype" in this
  // parameter-list-based model (there is no separate K&R/no-prototype bit).
  auto write_lock = LockForWrite();
  clike::Builder builder(*this);
  return builder.CreateFunctionType(builder.GetVoidType(), {},
                                    /*is_variadic=*/true);
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

bool TypeSystemClike::IsConst(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && t->IsConst();
}

uint32_t TypeSystemClike::IsHomogeneousAggregate(opaque_compiler_type_t type,
                                                 CompilerType *base_type_ptr) {
  // Lazily completes the type, so this needs the write lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return 0;
  clike::Type *record = Desugar(tt.get());
  if (!record->IsRecord())
    return 0;
  clike::Type *completed = CompleteTypeAssumingWriteLocked(record);
  if (!completed || !completed->IsComplete())
    return 0;
  uint32_t num_fields = 0;
  clike::Type *base_type = record->GetHomogeneousAggregateBase(num_fields);
  if (!base_type)
    return 0;
  if (base_type_ptr)
    *base_type_ptr = GetCompilerType(base_type);
  return num_fields;
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

bool TypeSystemClike::IsVectorType(opaque_compiler_type_t type,
                                   CompilerType *element_type, uint64_t *size) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return false;
  const clike::Type *t = Desugar(tt.get());
  if (!t->IsVector())
    return false;
  if (element_type)
    *element_type = GetCompilerType(t->GetArrayElementType());
  if (size) {
    if (std::optional<uint64_t> num = t->GetNumElements())
      *size = *num;
  }
  return true;
}

CompilerType
TypeSystemClike::GetFullyUnqualifiedType(opaque_compiler_type_t type) {
  // GetFullyUnqualifiedTypeImpl may allocate fresh Pointer/Array wrapper
  // nodes, so this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  return GetCompilerType(GetFullyUnqualifiedTypeImpl(t.get()));
}

clike::Type *TypeSystemClike::GetFullyUnqualifiedTypeImpl(clike::Type *t) {
  // Mirror TypeSystemClang::GetFullyUnqualifiedType: strip top-level
  // cv-qualifiers, and recurse through pointers/arrays so their pointee/element
  // is likewise fully unqualified (so `struct Foo * const` -> `struct Foo *`,
  // and `const char *` -> `char *`). Other sugar (typedefs, elaborated
  // spellings) is preserved.
  if (!t)
    return t;
  // Strip any stacked top-level cv-qualifiers.
  t = t->StripCVQualifiers();

  clike::Builder builder(*this);
  if (t->IsPointer()) {
    clike::Type *pointee = t->GetPointeeType();
    clike::Type *stripped = GetFullyUnqualifiedTypeImpl(pointee);
    if (stripped != pointee)
      return static_cast<clike::Type *>(
          builder.CreatePointerType(GetCompilerType(stripped))
              .GetOpaqueQualType());
    return t;
  }
  if (clike::Type *element = t->GetArrayElementType()) {
    clike::Type *stripped = GetFullyUnqualifiedTypeImpl(element);
    if (stripped != element)
      return static_cast<clike::Type *>(
          builder
              .CreateArrayType(GetCompilerType(stripped), t->GetNumElements())
              .GetOpaqueQualType());
    return t;
  }
  return t;
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

CompilerType
TypeSystemClike::GetDirectNestedTypeWithName(opaque_compiler_type_t type,
                                             llvm::StringRef name) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  CompleteTypeAssumingWriteLocked(t.get());
  return GetCompilerType(Desugar(t.get())->GetNestedTypeWithName(name));
}
