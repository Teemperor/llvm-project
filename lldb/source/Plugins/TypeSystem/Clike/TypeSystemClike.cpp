//===-- TypeSystemClike.cpp -------------------------------------------------===//
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

#include "ObjCMethodSignature.h"
#include "TypeName.h"

#include "Plugins/SymbolFile/DWARF/DWARFASTParserClike.h"
#include "Plugins/SymbolFile/DWARF/DWARFDIE.h"
#include "Plugins/SymbolFile/DWARF/SymbolFileDWARF.h"


#include "Plugins/Language/ObjC/ObjCLanguage.h"
#include "Plugins/LanguageRuntime/ObjC/ObjCLanguageRuntime.h"

#include "lldb/Core/DumpDataExtractor.h"
#include "lldb/Core/PluginManager.h"
#include "lldb/Host/StreamFile.h"
#include "lldb/Expression/UtilityFunction.h"
#include "lldb/Symbol/Block.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/Symbol.h"
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
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/bit.h"
#include "llvm/Support/MathExtras.h"

#include <algorithm>
#include <cstdio>

using namespace lldb_private;
using namespace lldb;

using clike_typesystem::Field;

using clike_typesystem::Desugar;

static std::optional<int64_t>
ReadVirtualBaseOffset(TypeSystemClike &ts, clike_typesystem::RecordType *derived,
                      const clike_typesystem::BaseClass &base,
                      ValueObject *valobj) {
  if (!valobj)
    return std::nullopt;
  ExecutionContext exe_ctx(valobj->GetExecutionContextRef());
  Process *process = exe_ctx.GetProcessPtr();
  if (!process)
    return std::nullopt;

  // The vbase-offset-offset is recovered from the DWARF location expression on
  // the inheritance DIE. Darwin's dsymutil strips that expression from the
  // .dSYM; recomputing it from a synthesized Clang vtable layout arrives with
  // ClangASTGenerator.
  std::optional<uint64_t> vbase_offset_offset = base.vbase_offset_offset;
  if (!vbase_offset_offset)
    return std::nullopt;

  // The vtable pointer sits at the start of the (derived) object. When the
  // vbase is reached transparently through a pointer (see the transparent
  // pointer forwarding in GetChildCompilerTypeAtIndex), `valobj` is the pointer
  // itself: the derived object is what it points to, so use the pointee (load)
  // address rather than where the pointer is stored.
  lldb::addr_t obj_addr = LLDB_INVALID_ADDRESS;
  if (valobj->IsPointerType()) {
    ValueObject::AddrAndType ptr = valobj->GetPointerValue();
    if (ptr.type != eAddressTypeLoad)
      return std::nullopt;
    obj_addr = ptr.address;
  } else {
    ValueObject::AddrAndType addr = valobj->GetAddressOf();
    if (addr.type != eAddressTypeLoad)
      return std::nullopt;
    obj_addr = addr.address;
  }
  if (obj_addr == LLDB_INVALID_ADDRESS || obj_addr == 0)
    return std::nullopt;

  llvm::Expected<lldb::addr_t> vtable_ptr_or_err =
      process->ReadPointerFromMemory(obj_addr);
  if (!vtable_ptr_or_err) {
    llvm::consumeError(vtable_ptr_or_err.takeError());
    return std::nullopt;
  }
  const lldb::addr_t vtable_ptr = *vtable_ptr_or_err;
  if (vtable_ptr == LLDB_INVALID_ADDRESS)
    return std::nullopt;

  Status err;
  const uint32_t addr_size = process->GetAddressByteSize();
  int64_t offset = process->ReadSignedIntegerFromMemory(
      vtable_ptr - *vbase_offset_offset, addr_size, INT64_MAX, err);
  if (err.Fail() || offset == INT64_MAX)
    return std::nullopt;
  return offset;
}



// Look through transparent sugar (typedef/cv/elaborated) for the pointer-auth
// qualifier that applies to \p type, or null if none. The `__ptrauth` qualifier
// sits on the outermost declarator, so only see-through sugar is peeled.
static const clike_typesystem::PtrAuthType *
FindPtrAuthType(const clike_typesystem::Type *t) {
  while (t) {
    if (auto *pa = llvm::dyn_cast<clike_typesystem::PtrAuthType>(t))
      return pa;
    if (auto *sugar = llvm::dyn_cast<clike_typesystem::SugarType>(t)) {
      t = sugar->GetUnderlyingType();
      continue;
    }
    break;
  }
  return nullptr;
}

LLDB_PLUGIN_DEFINE(TypeSystemClike)

char TypeSystemClike::ID;

TypeSystemClike::TypeSystemClike(llvm::StringRef name,
                                 clike_typesystem::LanguageOpts opts)
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
/// (LLVM_USE_RW_MUTEX_IMPL, see llvm/Support/RWMutex.h), pthread_rwlock_rdlock /
/// _wrlock return EDEADLK when the calling thread already holds the write lock,
/// and SmartRWMutex discards that error -- so a re-entrant acquisition here
/// silently does not lock and execution carries on with no mutual exclusion at
/// all, while the same code deadlocks on a host where RWMutex is
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
  // control flow alone on the Objective-C runtime-completion path -- so a module
  // one of them covers has to lock the same instances the scratch would. If it
  // locked only itself, that runtime-completion path would then reach into the
  // scratch and take its locks in the opposite order.
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
  // so it is available without parsing any type -- which is what lets the set be
  // complete before the first query, rather than only after one has already
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

CompilerType TypeSystemClike::GetCompilerType(clike_typesystem::Type *type) {
  return CompilerType(weak_from_this(), type);
}

llvm::Expected<lldb::TypeSystemSP>
TypeSystemClike::Create(llvm::StringRef name, llvm::Triple triple) {
  llvm::Expected<clike_typesystem::LanguageOpts> opts =
      clike_typesystem::LanguageOpts::Create(std::move(triple));
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
  // TypeSystemClike's CompilerDecls wrap a clike_typesystem::Decl reference.
  SharedLockedDecl decl = GetDeclForRead(opaque_decl);
  if (!decl)
    return ConstString();
  return std::visit(
      [](const auto *member) { return ConstString(member->name.GetName()); },
      decl->payload);
}

ConstString TypeSystemClike::DeclGetMangledName(void *opaque_decl) {
  SharedLockedDecl decl = GetDeclForRead(opaque_decl);
  if (!decl)
    return ConstString();
  return std::visit(
      [](const auto *member) {
        return ConstString(member->mangled_name.GetName());
      },
      decl->payload);
}

CompilerType TypeSystemClike::GetTypeForDecl(void *opaque_decl) {
  SharedLockedDecl decl = GetDeclForRead(opaque_decl);
  if (!decl)
    return CompilerType();
  return std::visit(
      [this](const auto *member) { return GetCompilerType(&member->type.Get()); },
      decl->payload);
}

Scalar TypeSystemClike::DeclGetConstantValue(void *opaque_decl) {
  SharedLockedDecl decl = GetDeclForRead(opaque_decl);
  if (!decl)
    return Scalar();
  const auto *const *member =
      std::get_if<const clike_typesystem::StaticDataMember *>(&decl->payload);
  if (!member || !(*member)->HasConstValue())
    return Scalar();
  clike_typesystem::Type *desugared = (*member)->type.Get().Desugar();
  std::optional<uint64_t> byte_size = desugared->GetByteSize();
  if (!byte_size)
    return Scalar();
  // Interpret the raw constant bits using the member type's signedness so a
  // signed integral member (e.g. `static constexpr long = 47`) reads back
  // correctly.
  bool is_signed = desugared->GetEncoding() == lldb::eEncodingSint;
  llvm::APInt value(*byte_size * 8, *(*member)->const_value, is_signed);
  return Scalar(llvm::APSInt(value, !is_signed));
}

ConstString TypeSystemClike::DeclContextGetName(void *opaque_decl_ctx) {
  // A TypeSystemClike CompilerDeclContext wraps a clike_typesystem::Namespace (the
  // global namespace is a null opaque pointer, which is never a valid
  // CompilerDeclContext).
  if (!opaque_decl_ctx)
    return ConstString();
  auto read_lock = LockForRead();
  auto *ns = static_cast<const clike_typesystem::Namespace *>(opaque_decl_ctx);
  return ConstString(ns->GetName().GetName());
}

ConstString
TypeSystemClike::DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) {
  if (!opaque_decl_ctx)
    return ConstString();
  auto read_lock = LockForRead();
  auto *ns = static_cast<const clike_typesystem::Namespace *>(opaque_decl_ctx);
  // Build "A::B::C" from the namespace chain, skipping the (transparent)
  // inline namespaces so the spelling matches the source.
  llvm::SmallVector<llvm::StringRef, 4> parts;
  for (const clike_typesystem::Namespace *cur = ns; cur; cur = cur->GetParent()) {
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
  for (auto *ns = static_cast<const clike_typesystem::Namespace *>(opaque_decl_ctx);
       ns; ns = ns->GetParent())
    context.push_back({CompilerContextKind::Namespace,
                       ConstString(ns->GetName().GetName())});
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
  auto *self = static_cast<const clike_typesystem::Namespace *>(opaque_decl_ctx);
  auto *other =
      static_cast<const clike_typesystem::Namespace *>(other_opaque_decl_ctx);
  for (const clike_typesystem::Namespace *cur = other; cur;
       cur = cur->GetParent()) {
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

bool TypeSystemClike::IsArrayTypeImpl(const clike_typesystem::Type *type,
                                    CompilerType *element_type, uint64_t *size,
                                    bool *is_incomplete) {
  if (element_type)
    element_type->Clear();
  if (size)
    *size = 0;
  if (is_incomplete)
    *is_incomplete = false;

  auto *array = llvm::dyn_cast_or_null<clike_typesystem::ArrayType>(
      type ? Desugar(type) : nullptr);
  if (!array)
    return false;
  // A vector type (DW_AT_GNU_vector) is laid out like an array but is not an
  // array type (matching TypeSystemClang, where a Vector/ExtVector is distinct
  // from ConstantArray). Reporting it as an array makes char-element vectors
  // match the char-array-to-string formatter and mis-render.
  if (array->IsVector())
    return false;

  if (element_type)
    *element_type = GetCompilerType(array->GetElementType());
  if (std::optional<uint64_t> num_elements = array->GetNumElements()) {
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
  auto *builtin =
      llvm::dyn_cast<clike_typesystem::BuiltinType>(Desugar(t.get()));
  return builtin && builtin->IsChar();
}

bool TypeSystemClike::IsCompleteType(opaque_compiler_type_t type) {
  if (!type)
    return false;
  // Mirror TypeSystemClang: complete the type now (if it has a definition in
  // the debug info) so we can give the caller an accurate answer about whether
  // the type actually has a definition, rather than just its current internal
  // completeness state. Builtins, pointers, references, etc. report complete
  // via clike_typesystem::Type::IsComplete(); only records/enums that were parsed
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
  return t && llvm::isa<clike_typesystem::FunctionType>(Desugar(t.get()));
}

size_t
TypeSystemClike::GetNumberOfFunctionArguments(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return 0;
  if (auto *fn =
          llvm::dyn_cast<clike_typesystem::FunctionType>(Desugar(t.get())))
    return fn->GetNumParameters();
  return 0;
}

CompilerType
TypeSystemClike::GetFunctionArgumentAtIndex(opaque_compiler_type_t type,
                                          const size_t index) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  if (auto *fn =
          llvm::dyn_cast<clike_typesystem::FunctionType>(Desugar(t.get())))
    return GetCompilerType(fn->GetParameterAtIndex(index));
  return CompilerType();
}

bool TypeSystemClike::IsFunctionPointerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  auto *ptr =
      llvm::dyn_cast<clike_typesystem::PointerType>(Desugar(t.get()));
  return ptr && ptr->IsFunctionPointer();
}

bool TypeSystemClike::IsMemberFunctionPointerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  auto *mp =
      llvm::dyn_cast<clike_typesystem::MemberPointerType>(Desugar(t.get()));
  return mp && mp->IsMemberFunctionPointer();
}

bool TypeSystemClike::IsMemberDataPointerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  auto *mp =
      llvm::dyn_cast<clike_typesystem::MemberPointerType>(Desugar(t.get()));
  return mp && !mp->IsMemberFunctionPointer();
}

bool TypeSystemClike::IsBlockPointerType(
    opaque_compiler_type_t type, CompilerType *function_pointer_type_ptr) {
  // May allocate a fresh pointer type below, so this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return false;
  auto *ptr =
      llvm::dyn_cast<clike_typesystem::BlockPointerType>(Desugar(t.get()));
  if (!ptr)
    return false;
  // Report the corresponding function-pointer type (a plain pointer to the
  // block's function type), mirroring TypeSystemClang.
  if (function_pointer_type_ptr)
    *function_pointer_type_ptr =
        clike_typesystem::Builder(*this).CreatePointerType(
            GetCompilerType(ptr->GetPointeeType()));
  return true;
}

bool TypeSystemClike::IsIntegerType(opaque_compiler_type_t type,
                                  bool &is_signed) {
  is_signed = false;
  SharedLockedType tt = GetTypeForRead(type);
  return tt && IsIntegerTypeImpl(tt.get(), is_signed);
}

bool TypeSystemClike::IsIntegerTypeImpl(const clike_typesystem::Type *t,
                                      bool &is_signed) {
  is_signed = false;
  // Only builtin integer types (and enumerations, matching TypeSystemClike's
  // existing treatment) are integers. Pointers report an unsigned encoding for
  // value extraction but must NOT be classified as integers, or e.g. DIL
  // array-subscript index checking would accept a pointer index.
  t = Desugar(t);
  if (!llvm::isa<clike_typesystem::BuiltinType, clike_typesystem::EnumType>(t))
    return false;
  // std::nullptr_t is a builtin with an unsigned (pointer-width) encoding but
  // is not an integer type.
  if (auto *bt = llvm::dyn_cast<clike_typesystem::BuiltinType>(t))
    if (bt->GetBuiltinKind() == clike_typesystem::BuiltinKind::NullPtr)
      return false;
  switch (t->GetEncoding()) {
  case eEncodingSint:
    is_signed = true;
    return true;
  case eEncodingUint:
    return true;
  default:
    return false;
  }
}

bool TypeSystemClike::IsScopedEnumerationType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  if (auto *enum_type =
          llvm::dyn_cast<clike_typesystem::EnumType>(Desugar(t.get())))
    return enum_type->IsScoped();
  return false;
}

bool TypeSystemClike::IsEnumerationType(opaque_compiler_type_t type,
                                      bool &is_signed) {
  is_signed = false;
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  if (auto *enum_type =
          llvm::dyn_cast<clike_typesystem::EnumType>(Desugar(t.get()))) {
    is_signed = enum_type->IsSigned();
    return true;
  }
  return false;
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

  clike_typesystem::Type *t = Desugar(tt.get());

  auto set_target = [&](clike_typesystem::Type *pointee) {
    if (target_type && pointee)
      target_type->SetCompilerType(weak_from_this(),
                                   static_cast<opaque_compiler_type_t>(pointee));
  };

  // An Objective-C object is always accessed through a pointer (`Foo *` / `id`).
  // A pointer to an ObjC interface is a possible dynamic type when checking ObjC.
  if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(t)) {
    clike_typesystem::Type *pointee = ptr->GetPointeeType()->Desugar();
    if (llvm::isa<clike_typesystem::ObjCInterfaceType>(pointee)) {
      if (check_objc) {
        set_target(pointee);
        return true;
      }
      return false;
    }
    // `id` is modeled as a pointer to the opaque `objc_object` record.
    if (check_objc && clike_typesystem::IsOpaqueObjCObjectRecord(pointee)) {
      set_target(pointee);
      return true;
    }
  }

  // C++: a pointer or reference to a polymorphic class (one that -- or whose
  // base -- has a vtable) is a possible dynamic type. The object's vtable
  // pointer is followed to its RTTI to find the most-derived type (see the
  // Itanium ABI language runtime). Mirror TypeSystemClang::IsPossibleDynamicType:
  // also accept a pointer to `void` (an opaque pointer that may really point at
  // a polymorphic object). References only appear in C++, so they always imply
  // the C++ path.
  clike_typesystem::Type *pointee = nullptr;
  bool is_reference = false;
  if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(t)) {
    pointee = ptr->GetPointeeType()->Desugar();
  } else if (auto *ref = llvm::dyn_cast<clike_typesystem::ReferenceType>(t)) {
    pointee = ref->GetPointeeType()->Desugar();
    is_reference = true;
  }

  if (pointee) {
    // `void *` -- accept as a possible (watered-down) dynamic pointer, matching
    // TypeSystemClang. This is accepted regardless of the check_cplusplus /
    // check_objc flags (clang's Builtin::Void / UnknownAny pointee case is not
    // gated on them): an opaque pointer may point at either a polymorphic C++
    // object or an ObjC object, and the ObjC runtime relies on this to
    // dynamic-type a `void *` exception pointer. A reference can't be to void.
    if (!is_reference) {
      if (auto *bt = llvm::dyn_cast<clike_typesystem::BuiltinType>(pointee)) {
        if (bt->IsVoid()) {
          set_target(pointee);
          return true;
        }
      }
    }
    // A pointer/reference to a class: dynamic iff the class is polymorphic.
    // Complete the (possibly forward-declared) record first, since the vtable
    // fact is only known after completion -- this mirrors clang's
    // GetCompleteType() -> isDynamicClass() fallback.
    if (check_cplusplus && llvm::isa<clike_typesystem::ClassType>(pointee)) {
      CompleteTypeAssumingWriteLocked(pointee);
      if (pointee->IsPolymorphic()) {
        set_target(pointee);
        return true;
      }
      return false;
    }
  }

  return false;
}

bool TypeSystemClike::IsPointerType(opaque_compiler_type_t type,
                                  CompilerType *pointee_type) {
  if (pointee_type)
    pointee_type->Clear();
  SharedLockedType tt = GetTypeForRead(type);
  auto *ptr = llvm::dyn_cast_or_null<clike_typesystem::PointerType>(
      tt ? Desugar(tt.get()) : nullptr);
  if (!ptr)
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(ptr->GetPointeeType());
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
  // Void is the builtin with an invalid encoding spelled "void" (see
  // BuiltinTypes.cpp). Identify it by spelling so a void reached through another
  // Context (e.g. an expression result) is still recognized.
  auto *builtin =
      llvm::dyn_cast<clike_typesystem::BuiltinType>(Desugar(t.get()));
  return builtin && builtin->GetEncoding() == lldb::eEncodingInvalid &&
         builtin->GetName().GetName() == "void";
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
  clike_typesystem::Type *completed = CompleteTypeAssumingWriteLocked(t.get());
  return completed && completed->IsComplete();
}

clike_typesystem::Type *TypeSystemClike::CompleteTypeAssumingWriteLocked(
    clike_typesystem::Type *type) {
  if (!type)
    return nullptr;
  // See through typedefs/qualifiers: it is the underlying record that carries
  // completion state and is registered in the forward-declaration map.
  clike_typesystem::Type *t = Desugar(type);
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
  auto *record =
      llvm::dyn_cast_or_null<clike_typesystem::RecordType>(Desugar(t.get()));
  if (!record)
    return false;
  clike_typesystem::Type *completed = CompleteTypeAssumingWriteLocked(record);
  return !(completed && completed->IsComplete());
}

void TypeSystemClike::CompleteMemberFunctions(clike_typesystem::Type *type) {
  if (!type)
    return;
  auto write_lock = LockForWrite();
  CompleteMemberFunctionsAssumingWriteLocked(type);
}

void TypeSystemClike::CompleteMemberFunctionsAssumingWriteLocked(
    clike_typesystem::Type *type) {
  if (!type)
    return;
  // See through typedefs/qualifiers to the underlying record, mirroring
  // GetCompleteType.
  auto *record =
      llvm::dyn_cast_or_null<clike_typesystem::RecordType>(Desugar(type));
  if (!record || record->AreMemberFunctionsParsed())
    return;
  // Member functions live on the completed record, and the DWARF parser learns
  // the record's defining DIE while completing it, so complete it first.
  CompleteTypeAssumingWriteLocked(record);
  if (auto *parser =
          llvm::dyn_cast_or_null<DWARFASTParserClike>(GetDWARFParser()))
    parser->CompleteMemberFunctionsFromDWARF(*record);
}

std::vector<CompilerDeclContext>
TypeSystemClike::GetUsingDirectiveNamespaces(Block &block) {
  std::vector<CompilerDeclContext> namespaces;
  auto *parser = llvm::dyn_cast_or_null<DWARFASTParserClike>(GetDWARFParser());
  if (!parser)
    return namespaces;
  auto *dwarf = llvm::dyn_cast_or_null<plugin::dwarf::SymbolFileDWARF>(
      block.GetSymbolFile());
  if (!dwarf)
    return namespaces;
  plugin::dwarf::DWARFDIE block_die = dwarf->GetDIE(block.GetID());
  if (!block_die)
    return namespaces;
  parser->CollectUsingDirectiveNamespaces(block_die, namespaces);
  return namespaces;
}

std::vector<std::pair<ConstString, CompilerDeclContext>>
TypeSystemClike::GetUsingDeclarations(Block &block) {
  std::vector<std::pair<ConstString, CompilerDeclContext>> decls;
  auto *parser = llvm::dyn_cast_or_null<DWARFASTParserClike>(GetDWARFParser());
  if (!parser)
    return decls;
  auto *dwarf = llvm::dyn_cast_or_null<plugin::dwarf::SymbolFileDWARF>(
      block.GetSymbolFile());
  if (!dwarf)
    return decls;
  plugin::dwarf::DWARFDIE block_die = dwarf->GetDIE(block.GetID());
  if (!block_die)
    return decls;
  parser->CollectUsingDeclarations(block_die, decls);
  return decls;
}

CompilerType TypeSystemClike::GetOwningClassForFunction(Block &function_block) {
  auto *parser = llvm::dyn_cast_or_null<DWARFASTParserClike>(GetDWARFParser());
  if (!parser)
    return CompilerType();
  auto *dwarf = llvm::dyn_cast_or_null<plugin::dwarf::SymbolFileDWARF>(
      function_block.GetSymbolFile());
  if (!dwarf)
    return CompilerType();
  plugin::dwarf::DWARFDIE block_die = dwarf->GetDIE(function_block.GetID());
  if (!block_die)
    return CompilerType();
  return parser->GetOwningClassForFunctionFromDWARF(block_die);
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
  clike_typesystem::Builder builder(*this);
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
  clike_typesystem::Builder builder(*this);
  if (is_signed)
    return builder.GetBuiltinType("__ptrdiff_t", byte_size,
                                  lldb::eEncodingSint, lldb::eFormatDecimal);
  return builder.GetBuiltinType("__ptrdiff_t unsigned", byte_size,
                                lldb::eEncodingUint, lldb::eFormatUnsigned);
}

unsigned TypeSystemClike::GetPtrAuthKey(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (auto *pa = FindPtrAuthType(t.get()))
    return pa->GetKey();
  return 0;
}

unsigned TypeSystemClike::GetPtrAuthDiscriminator(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (auto *pa = FindPtrAuthType(t.get()))
    return pa->GetExtraDiscriminator();
  return 0;
}

bool TypeSystemClike::GetPtrAuthAddressDiversity(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (auto *pa = FindPtrAuthType(t.get()))
    return pa->IsAddressDiscriminated();
  return false;
}

bool TypeSystemClike::HasPointerAuthQualifier(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return FindPtrAuthType(t.get()) != nullptr;
}

// If `t` is an incomplete record whose spelling carries template arguments
// (contains `<`), complete it so its modeled template arguments are available.
// Building the reconstructed display name needs those arguments; without them
// the raw DWARF spelling is used verbatim, which renders enum-typed non-type
// arguments as `(EnumType)0` rather than the enumerator name. Recurses into
// type-kind template arguments (`Foo<Bar<1L>>`): reconstructing `Foo`'s name
// needs `Bar`'s own arguments too, since `Bar<1L>`'s spelling is embedded in
// `Foo`'s DWARF name.
void TypeSystemClike::CompleteTemplateInstantiationForName(
    clike_typesystem::Type *t) {
  auto write_lock = LockForWrite();
  CompleteTemplateInstantiationForNameAssumingWriteLocked(t);
}

void TypeSystemClike::CompleteTemplateInstantiationForNameAssumingWriteLocked(
    clike_typesystem::Type *t) {
  auto *rec = llvm::dyn_cast_or_null<clike_typesystem::RecordType>(t);
  if (!rec || !rec->GetName().GetName().contains('<'))
    return;
  if (!rec->IsComplete())
    CompleteTypeAssumingWriteLocked(rec);
  for (uint32_t i = 0, e = rec->GetNumTemplateArguments(); i != e; ++i) {
    const clike_typesystem::TemplateArgument *arg =
        rec->GetTemplateArgumentAtIndex(i);
    if (arg->kind == lldb::eTemplateArgumentKindType)
      CompleteTemplateInstantiationForNameAssumingWriteLocked(&arg->type->Get());
  }
}

ConstString TypeSystemClike::GetTypeName(opaque_compiler_type_t type,
                                       bool BaseOnly) {
  // A class-template instantiation's name is reconstructed from its modeled
  // template arguments (so an enum-typed non-type argument prints as
  // `EnumType::Member` rather than the DWARF producer's `(EnumType)0`). Those
  // arguments only exist once the record is completed, so complete it now --
  // the only part of naming a type that needs the TypeSystem. This may
  // mutate, so it needs the write lock.
  LockedType t = GetTypeForWrite(type);
  return GetTypeNameAssumingWriteLocked(t.get(), BaseOnly);
}

ConstString TypeSystemClike::GetTypeNameAssumingWriteLocked(
    clike_typesystem::Type *t, bool BaseOnly) {
  if (!t)
    return ConstString();
  CompleteTemplateInstantiationForNameAssumingWriteLocked(t);
  return ConstString(clike_typesystem::BuildCanonicalName(t, BaseOnly));
}

ConstString TypeSystemClike::GetDisplayTypeName(opaque_compiler_type_t type) {
  LockedType t = GetTypeForWrite(type);
  return GetDisplayTypeNameAssumingWriteLocked(t.get());
}

ConstString
TypeSystemClike::GetDisplayTypeNameAssumingWriteLocked(clike_typesystem::Type *t) {
  if (!t)
    return ConstString();
  CompleteTemplateInstantiationForNameAssumingWriteLocked(t);
  return ConstString(clike_typesystem::BuildDisplayName(t));
}

uint32_t
TypeSystemClike::GetTypeInfo(opaque_compiler_type_t type,
                           CompilerType *pointee_or_element_compiler_type) {
  if (pointee_or_element_compiler_type)
    pointee_or_element_compiler_type->Clear();
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return 0;
  const clike_typesystem::Type *t = tt.get();
  // Hand back the element/pointee type when asked; callers such as
  // ValueObject::GetPointeeData rely on it to know how to read array elements
  // or dereference pointers/references.
  if (pointee_or_element_compiler_type) {
    clike_typesystem::Type *inner = nullptr;
    if (auto *array = llvm::dyn_cast<clike_typesystem::ArrayType>(Desugar(t)))
      inner = array->GetElementType();
    else if (auto *ptr =
                 llvm::dyn_cast<clike_typesystem::PointerType>(Desugar(t)))
      inner = ptr->GetPointeeType();
    else if (auto *ref =
                 llvm::dyn_cast<clike_typesystem::ReferenceType>(Desugar(t)))
      inner = ref->GetPointeeType();
    if (inner)
      *pointee_or_element_compiler_type = GetCompilerType(inner);
  }
  return t->GetTypeInfo();
}

LanguageType TypeSystemClike::GetMinimumLanguage(opaque_compiler_type_t type) {
  // TypeSystemClike models C/C++/Objective-C++ types. Reporting C++ is what puts
  // types in the C++ formatter category, so that e.g. the libc++ container
  // data formatters are consulted (see FormatManager::GetCandidateLanguages).
  //
  // Exception: a pointer to a plain scalar/enum (e.g. `int *`, `enum E *`) is a
  // C construct. Reporting it as C++ makes CPlusPlusLanguage::IsNilReference
  // treat a null such pointer as a nil object reference and print it as "NULL"
  // instead of its address; TypeSystemClang reports C for these, printing 0x0.
  // Pointers to records keep reporting C++ so class data formatters still fire.
  SharedLockedType tt = GetTypeForRead(type);
  if (tt) {
    const clike_typesystem::Type *t = Desugar(tt.get());
    // Resolve through a reference to what it refers to first, matching
    // TypeSystemClang's GetCanonicalQualType(type).getNonReferenceType():
    // `Shape &` must report C++ (so e.g. GetVTable's language-runtime lookup
    // and class formatters fire) exactly like `Shape` does, not fall through
    // to the "plain non-record type" C default below (a reference is never
    // itself the "plain scalar" case that default exists for).
    if (auto *ref = llvm::dyn_cast<clike_typesystem::ReferenceType>(t))
      t = ref->GetPointeeType()->Desugar();
    // An Objective-C interface (or a pointer to one, i.e. an ObjC object like
    // `NSObject *`) is an Objective-C construct. Reporting ObjC is what routes
    // it to the ObjC language runtime for dynamic-type resolution (see
    // ValueObjectDynamicValue::UpdateValue).
    if (llvm::isa<clike_typesystem::ObjCInterfaceType>(t))
      return eLanguageTypeObjC;
    if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(t)) {
      const clike_typesystem::Type *pointee = Desugar(ptr->GetPointeeType());
      // An ObjC object pointer (`NSObject *`, or the `id`/`Class` idiom -- a
      // pointer to the opaque `objc_object`/`objc_class` record) is routed to
      // the ObjC runtime for dynamic-type resolution.
      if (clike_typesystem::IsObjCObjectType(pointee))
        return eLanguageTypeObjC;
      if (!llvm::isa_and_nonnull<clike_typesystem::RecordType>(pointee))
        return eLanguageTypeC;
    } else if (!llvm::isa<clike_typesystem::RecordType>(t)) {
      // A plain (non-pointer, non-record, non-ObjC) type -- e.g. `int` -- is
      // a C construct, matching TypeSystemClang's default fallthrough
      // (eLanguageTypeC) for anything that isn't a CXXRecordDecl/ObjC
      // construct/pointer-to-record. Records themselves keep falling through
      // to eLanguageTypeC_plus_plus below so C++ class formatters still fire.
      return eLanguageTypeC;
    }
  }
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
  if (auto *array = llvm::dyn_cast<clike_typesystem::ArrayType>(Desugar(t.get())))
    return GetCompilerType(array->GetElementType());
  return CompilerType();
}

CompilerType TypeSystemClike::GetArrayType(opaque_compiler_type_t type,
                                         uint64_t size) {
  // Allocates a fresh ArrayType node.
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  // Build an array of `size` elements of `type` (size 0 => unbounded). Used by
  // the formatter matcher to form a typedef-stripped array candidate name (e.g.
  // `MCHAR[5]` -> `char[5]`), so char-array-of-typedef matches the char[] string
  // summary.
  return clike_typesystem::Builder(*this).CreateArrayType(
      GetCompilerType(t.get()),
      size ? std::optional<uint64_t>(size)
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
  if (auto *enum_type =
          llvm::dyn_cast<clike_typesystem::EnumType>(Desugar(t.get())))
    return GetCompilerType(enum_type->GetUnderlyingType());
  return CompilerType();
}

void TypeSystemClike::ForEachEnumerator(
    opaque_compiler_type_t type,
    std::function<bool(const CompilerType &integer_type, ConstString name,
                       const llvm::APSInt &value)> const &callback) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return;
  auto *enum_type =
      llvm::dyn_cast<clike_typesystem::EnumType>(Desugar(t.get()));
  if (!enum_type)
    return;

  CompilerType integer_type = GetCompilerType(enum_type->GetUnderlyingType());
  const bool is_signed = enum_type->IsSigned();
  // Enumerator values are stored as raw bits; recover their APSInt using the
  // underlying type's width so signed values sign-extend correctly. Read the
  // byte size directly off the underlying Type (not via
  // integer_type.GetByteSize(), which would call back into this instance's
  // own GetBitSize and re-acquire the lock already held above).
  unsigned bit_width = 64;
  if (std::optional<uint64_t> byte_size =
          enum_type->GetUnderlyingType()->GetByteSize())
    bit_width = static_cast<unsigned>(*byte_size * 8);

  for (const clike_typesystem::Enumerator &enumerator :
       enum_type->GetEnumerators()) {
    llvm::APSInt value(llvm::APInt(bit_width, enumerator.value, is_signed),
                       !is_signed);
    if (!callback(integer_type, ConstString(enumerator.name.GetName()), value))
      break;
  }
}

int TypeSystemClike::GetFunctionArgumentCount(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return -1;
  if (auto *fn =
          llvm::dyn_cast<clike_typesystem::FunctionType>(Desugar(t.get())))
    return static_cast<int>(fn->GetNumParameters());
  return -1;
}

CompilerType
TypeSystemClike::GetFunctionArgumentTypeAtIndex(opaque_compiler_type_t type,
                                              size_t idx) {
  return GetFunctionArgumentAtIndex(type, idx);
}

CompilerType TypeSystemClike::GetFunctionReturnType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  if (auto *fn =
          llvm::dyn_cast<clike_typesystem::FunctionType>(Desugar(t.get())))
    return GetCompilerType(fn->GetReturnType());
  return CompilerType();
}

size_t TypeSystemClike::GetNumMemberFunctions(opaque_compiler_type_t type) {
  // Lazily parses member functions from DWARF, so this needs the write lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return 0;
  clike_typesystem::Type *t = Desugar(tt.get());
  // Member functions of a record; ObjC methods of an interface (reached through
  // a pointer, as always for ObjC). They are parsed lazily.
  clike_typesystem::Type *bearer = GetObjCBaseClassBearingType(tt.get());
  if (auto *iface = llvm::dyn_cast<clike_typesystem::ObjCInterfaceType>(bearer)) {
    CompleteMemberFunctionsAssumingWriteLocked(iface);
    return iface->GetNumObjCMethods();
  }
  if (auto *record = llvm::dyn_cast<clike_typesystem::RecordType>(t)) {
    CompleteMemberFunctionsAssumingWriteLocked(record);
    return record->GetNumMemberFunctions();
  }
  return 0;
}

TypeMemberFunctionImpl
TypeSystemClike::GetMemberFunctionAtIndex(opaque_compiler_type_t type,
                                        size_t idx) {
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return TypeMemberFunctionImpl();
  clike_typesystem::Type *t = Desugar(tt.get());

  // Objective-C methods (reached through the interface, possibly via a pointer).
  clike_typesystem::Type *bearer = GetObjCBaseClassBearingType(tt.get());
  if (auto *iface = llvm::dyn_cast<clike_typesystem::ObjCInterfaceType>(bearer)) {
    CompleteMemberFunctionsAssumingWriteLocked(iface);
    const clike_typesystem::ObjCMethod *method = iface->GetObjCMethodAtIndex(idx);
    if (!method)
      return TypeMemberFunctionImpl();
    // Clang names the member function by the method's selector (e.g. `foo:` /
    // `init`). The stored name is the full `-[Class sel:]`; recover the
    // selector from it.
    std::string name = method->name.GetName().str();
    if (std::optional<const ObjCLanguage::ObjCMethodName> parsed =
            ObjCLanguage::ObjCMethodName::Create(name, /*strict=*/false))
      name = parsed->GetSelector().str();
    MemberFunctionKind kind = method->is_class_method
                                  ? lldb::eMemberFunctionKindStaticMethod
                                  : lldb::eMemberFunctionKindInstanceMethod;
    return TypeMemberFunctionImpl(GetCompilerType(&method->type.Get()),
                                  CompilerDecl(), name, kind);
  }

  auto *record = llvm::dyn_cast<clike_typesystem::RecordType>(t);
  if (!record)
    return TypeMemberFunctionImpl();
  CompleteMemberFunctionsAssumingWriteLocked(record);
  const clike_typesystem::MemberFunction *method =
      record->GetMemberFunctionAtIndex(idx);
  if (!method)
    return TypeMemberFunctionImpl();

  MemberFunctionKind kind;
  switch (method->kind) {
  case clike_typesystem::MemberFunctionKind::Constructor:
    kind = lldb::eMemberFunctionKindConstructor;
    break;
  case clike_typesystem::MemberFunctionKind::Destructor:
    kind = lldb::eMemberFunctionKindDestructor;
    break;
  case clike_typesystem::MemberFunctionKind::Method:
    kind = method->is_static ? lldb::eMemberFunctionKindStaticMethod
                             : lldb::eMemberFunctionKindInstanceMethod;
    break;
  }
  // Hand out a tagged Decl so GetMangledName/GetDemangledName can recover the
  // linkage name; the (return/argument) types come from the function type.
  CompilerDecl decl(this, const_cast<clike_typesystem::Decl *>(
                              m_context.GetOrCreateDecl(method)));
  return TypeMemberFunctionImpl(GetCompilerType(&method->type.Get()), decl,
                                method->name.GetName().str(), kind);
}

CompilerType TypeSystemClike::GetPointeeType(opaque_compiler_type_t type) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return CompilerType();
  const clike_typesystem::Type *desugared = Desugar(tt.get());
  // A `void *` points at the `void` builtin, so this is a valid CompilerType
  // for every pointer -- which is what callers like
  // CompilerType::IsPointerToVoid expect (and what TypeSystemClang, whose
  // `void` QualType is always valid, does).
  if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(desugared))
    return GetCompilerType(ptr->GetPointeeType());
  // A reference's "pointee" is the referenced type, matching
  // TypeSystemClang::GetPointeeType (clang::Type::getPointeeType() answers
  // both pointers and references).
  if (auto *ref = llvm::dyn_cast<clike_typesystem::ReferenceType>(desugared))
    return GetCompilerType(ref->GetPointeeType());
  return CompilerType();
}

CompilerType TypeSystemClike::GetPointerType(opaque_compiler_type_t type) {
  // Allocates a fresh PointerType node.
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  return clike_typesystem::Builder(*this).CreatePointerType(
      GetCompilerType(t.get()));
}

CompilerType
TypeSystemClike::GetLValueReferenceType(opaque_compiler_type_t type) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  return clike_typesystem::Builder(*this).CreateReferenceType(
      GetCompilerType(t.get()), /*is_rvalue=*/false);
}

CompilerType
TypeSystemClike::GetRValueReferenceType(opaque_compiler_type_t type) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  return clike_typesystem::Builder(*this).CreateReferenceType(
      GetCompilerType(t.get()), /*is_rvalue=*/true);
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
  auto strip_cv = [](clike_typesystem::Type *t) -> clike_typesystem::Type * {
    while (auto *cv = llvm::dyn_cast_or_null<clike_typesystem::CVQualifiedType>(t))
      t = cv->GetUnderlyingType();
    return t;
  };

  clike_typesystem::Type *t = strip_cv(tt.get());
  if (!t)
    return CompilerType();

  if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(t)) {
    clike_typesystem::Type *pointee = ptr->GetPointeeType();
    clike_typesystem::Type *stripped = strip_cv(pointee);
    if (stripped != pointee)
      return clike_typesystem::Builder(*this).CreatePointerType(
          GetCompilerType(stripped));
  }
  // The C-string array summary keys off an array type's spelling
  // (`char[N]` / `unsigned char[N]`), which clang produces by stripping the
  // element's cv-qualifiers. An array of `const char` is modeled here with the
  // cv-qualifier on the element (not the array), so rebuild the array with the
  // unqualified element so `const char[N]` also matches.
  if (auto *array = llvm::dyn_cast<clike_typesystem::ArrayType>(t)) {
    clike_typesystem::Type *element = array->GetElementType();
    clike_typesystem::Type *stripped = strip_cv(element);
    if (stripped != element)
      return clike_typesystem::Builder(*this).CreateArrayType(
          GetCompilerType(stripped), array->GetNumElements());
  }
  return GetCompilerType(t);
}

const llvm::fltSemantics &TypeSystemClike::GetFloatTypeSemantics(size_t byte_size,
                                                               Format format) {
  return m_context.GetLanguageOpts().GetFloatTypeSemantics(byte_size, format);
}

// An Objective-C class's DWARF-recorded byte size is a compile-time constant
// baked into whichever module's debug info produced it (and may be entirely
// absent, e.g. for a class -- like a tagged-pointer class such as NSIndexSet
// -- whose implementation lives in another image and was never given a
// DW_AT_byte_size in this module). It can also be smaller than the class's
// true instance size when ivars are added downstream of that module -- e.g. a
// class extension in a different image adds a hidden ivar to a superclass
// (see the hidden-ivars test): the subclass's own DW_AT_byte_size, emitted by
// a compile that only saw the superclass's public ivars, doesn't leave room
// for the hidden one. So the ObjC runtime's authoritative instance size is
// queried first, mirroring the ivar-offset override in
// GetChildCompilerTypeAtIndex -- getting this wrong silently undersizes the
// buffer used to materialize a whole-object expression result (e.g. `*k`),
// truncating trailing ivars, or (for a runtime-only-sized class with no
// DW_AT_byte_size at all) makes the class look incomplete.
static std::optional<uint64_t>
GetObjCRuntimeInstanceByteSize(const clike_typesystem::Type *t,
                              Process *process) {
  if (!process)
    return std::nullopt;
  ObjCLanguageRuntime *objc_runtime = ObjCLanguageRuntime::Get(*process);
  if (!objc_runtime)
    return std::nullopt;
  ConstString class_name(t->GetName().GetName());
  ObjCLanguageRuntime::ClassDescriptorSP descriptor =
      objc_runtime->GetClassDescriptorFromClassName(class_name);
  if (!descriptor)
    return std::nullopt;
  uint64_t instance_size = descriptor->GetInstanceSize();
  if (instance_size == 0)
    return std::nullopt;
  return instance_size;
}

llvm::Expected<uint64_t>
TypeSystemClike::GetBitSize(opaque_compiler_type_t type,
                          ExecutionContextScope *exe_scope) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return llvm::createStringError("invalid type");
  const clike_typesystem::Type *t = Desugar(tt.get());
  if (llvm::isa<clike_typesystem::ObjCInterfaceType>(t) && exe_scope) {
    if (lldb::ProcessSP process_sp = exe_scope->CalculateProcess()) {
      if (std::optional<uint64_t> instance_size =
              GetObjCRuntimeInstanceByteSize(t, process_sp.get()))
        return *instance_size * 8;
    }
  }
  if (std::optional<uint64_t> byte_size = t->GetByteSize())
    return *byte_size * 8;
  // Function types have no storage of their own. Matching TypeSystemClang
  // (clang models function types with a type size of 0), report a bit size of
  // 0 rather than an error. This keeps the dereferenced-value child of a
  // function pointer/reference from surfacing a size error (or a spurious byte
  // read) as its summary -- a zero-sized value simply has no value string.
  if (llvm::isa<clike_typesystem::FunctionType>(t))
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

namespace {
/// One Objective-C class's data as reported by the runtime's ClassDescriptor,
/// gathered without touching any TypeSystemClike (see GatherRuntimeObjCClassChain).
struct RuntimeObjCClassData {
  ConstString class_name;
  struct Method {
    std::string selector;
    std::string types;
    bool is_class_method;
  };
  std::vector<Method> methods;
  struct Ivar {
    std::string name;
    std::string type;
    lldb::addr_t offset_ptr;
    uint64_t size;
  };
  std::vector<Ivar> ivars;
};

/// Resolve \p class_name to a ClassDescriptor via the runtime's name->isa map,
/// falling back to the `OBJC_CLASS_$_<name>` symbol's address (which is
/// exactly the isa an instance of the class carries) for a dynamically
/// registered class that map is missing.
ObjCLanguageRuntime::ClassDescriptorSP
ResolveObjCClassDescriptor(ConstString class_name, Process &process,
                          ObjCLanguageRuntime &runtime) {
  if (ObjCLanguageRuntime::ClassDescriptorSP descriptor =
          runtime.GetClassDescriptorFromClassName(class_name))
    return descriptor;
  ConstString class_symbol(("OBJC_CLASS_$_" + class_name.GetStringRef()).str());
  SymbolContextList sc_list;
  process.GetTarget().GetImages().FindSymbolsWithNameAndType(
      class_symbol, lldb::eSymbolTypeObjCClass, sc_list);
  for (const SymbolContext &sc : sc_list) {
    if (!sc.symbol)
      continue;
    lldb::addr_t isa = sc.symbol->GetLoadAddress(&process.GetTarget());
    if (isa == LLDB_INVALID_ADDRESS)
      continue;
    if (ObjCLanguageRuntime::ClassDescriptorSP descriptor =
            runtime.GetClassDescriptorFromISA(isa))
      return descriptor;
  }
  return ObjCLanguageRuntime::ClassDescriptorSP();
}

/// Gather \p class_name's data, and transitively its superclass chain's (in
/// derived-to-base order), from the live ObjC runtime. Entirely independent of
/// any TypeSystemClike: every call this makes -- resolving a class by name or
/// isa, walking to its superclass, describing its methods/ivars -- goes
/// through Process/ObjCLanguageRuntime only, so this can run (and must run;
/// see TypeSystemClike::CreateRuntimeObjCInterface) before any TypeSystemClike
/// lock is taken. A cycle (a real ObjC hierarchy has none, but corrupted
/// runtime metadata might) is guarded by \p seen, mirroring the guard the
/// locked build phase used to get for free from its "already published" map.
llvm::SmallVector<RuntimeObjCClassData, 4>
GatherRuntimeObjCClassChain(ConstString class_name, Process &process,
                           ObjCLanguageRuntime &runtime) {
  llvm::SmallVector<RuntimeObjCClassData, 4> chain;
  llvm::DenseSet<const char *> seen;
  ConstString cur_name = class_name;
  ObjCLanguageRuntime::ClassDescriptorSP descriptor =
      ResolveObjCClassDescriptor(cur_name, process, runtime);
  while (descriptor && seen.insert(cur_name.GetCString()).second) {
    RuntimeObjCClassData data;
    data.class_name = cur_name;
    descriptor->Describe(
        /*superclass_func=*/nullptr,
        /*instance_method_func=*/
        [&](const char *name, const char *types) -> bool {
          data.methods.push_back({name, types, /*is_class_method=*/false});
          return false;
        },
        /*class_method_func=*/
        [&](const char *name, const char *types) -> bool {
          data.methods.push_back({name, types, /*is_class_method=*/true});
          return false;
        },
        [&](const char *name, const char *type, lldb::addr_t offset_ptr,
            uint64_t size) -> bool {
          if (name && name[0])
            data.ivars.push_back({name, type ? type : "", offset_ptr, size});
          return false;
        });

    // Chase the runtime's own superclass chain (rather than relying on any
    // debug info) so a class whose ivars are recovered from the runtime still
    // reports its inheritance -- e.g. `NSObject` as a child -- matching the
    // DWARF-derived case.
    ObjCLanguageRuntime::ClassDescriptorSP super_descriptor =
        descriptor->GetSuperclass();
    ConstString super_name =
        super_descriptor ? super_descriptor->GetClassName() : ConstString();
    if (super_name == cur_name)
      super_name = ConstString();
    chain.push_back(std::move(data));
    if (!super_name)
      break;
    cur_name = super_name;
    descriptor = super_descriptor;
  }
  return chain;
}
} // namespace

CompilerType
TypeSystemClike::CreateRuntimeObjCInterface(ConstString class_name,
                                          Process &process,
                                          ObjCLanguageRuntime &runtime) {
  // Fast path: an already-built runtime type needs no runtime interaction at
  // all -- check the cache before doing anything else.
  {
    auto read_lock = LockForRead();
    if (auto it = m_runtime_objc_types.find(class_name.GetStringRef());
        it != m_runtime_objc_types.end())
      return GetCompilerType(it->second);
  }

  // Phase 1: talk to the process/runtime -- entirely without holding this (or
  // any other) TypeSystemClike's lock. GetClassDescriptorFromClassName/FromISA
  // can transitively trigger AppleObjCRuntimeV2::UpdateISAToDescriptorMapIfNeeded,
  // which JIT-compiles and runs a whole utility-function expression the first
  // time (or after the process has continued) it is asked about a class; that
  // expression's own IR generation reaches back into a TypeSystemClike (see
  // ClangTypeConverter::Convert -> TypeSystemClike::GetBasicTypeFromAST),
  // possibly one that was never part of any lock set computed before this call
  // started (see GetLockOrder) -- doing that while this call already held a
  // lock is exactly the re-entrancy TestExpressionInSyscall's regression
  // diagnosed (see the revert of "Lock every TypeSystemClike..."). Only phase
  // 2 below, which just turns this already-gathered data into
  // clike_typesystem nodes via Builder, touches this instance and needs the
  // lock.
  llvm::SmallVector<RuntimeObjCClassData, 4> chain =
      GatherRuntimeObjCClassChain(class_name, process, runtime);
  if (chain.empty())
    return CompilerType();

  // Phase 2: build the chain's types, from the ultimate (possibly
  // already-cached) base up to `class_name` itself, so each subclass can link
  // to its already-built superclass.
  auto write_lock = LockForWrite();
  clike_typesystem::Type *previous_iface = nullptr;
  for (const RuntimeObjCClassData &data : llvm::reverse(chain)) {
    // Another lookup (on this or another thread) may have built this class --
    // including while phase 1 above ran unlocked -- so this recheck is not
    // just the class_name fast path above repeated: it is required for
    // correctness, not merely an optimization.
    if (auto it = m_runtime_objc_types.find(data.class_name.GetStringRef());
        it != m_runtime_objc_types.end()) {
      previous_iface = it->second;
      continue;
    }

    clike_typesystem::Builder builder(*this);
    CompilerType iface_ct = builder.CreateObjCInterfaceType(
        data.class_name.GetStringRef(), std::nullopt);
    auto *iface = llvm::cast<clike_typesystem::ObjCInterfaceType>(
        GetClikeType(iface_ct.GetOpaqueQualType()));
    // Publish before filling so a self-referential ivar can't recurse forever.
    m_runtime_objc_types[data.class_name.GetStringRef()] = iface;

    if (previous_iface)
      builder.SetObjCSuperClass(*iface, previous_iface);

    for (const RuntimeObjCClassData::Method &m : data.methods)
      clike_typesystem::AddRuntimeObjCMethod(
          builder, *iface, data.class_name.GetStringRef(),
          m.selector.c_str(), m.types.c_str(), m.is_class_method);

    for (const RuntimeObjCClassData::Ivar &ivar : data.ivars) {
      // The runtime stores a pointer to the ivar's (32-bit) byte offset. This
      // is a plain memory read (not a JIT'd call), so it is fine to do here
      // under the lock, same as it was under the old single-phase code.
      Status error;
      uint64_t byte_offset =
          process.ReadUnsignedIntegerFromMemory(ivar.offset_ptr, 4, 0, error);
      llvm::StringRef enc(ivar.type);
      CompilerType ivar_type = clike_typesystem::RealizeObjCEncoding(builder, enc);
      // Fall back to an opaque byte blob of the right size so a member we
      // can't decode still occupies its slot in the layout.
      if (!ivar_type)
        ivar_type = builder.CreateArrayType(
            builder.GetBuiltinType("char", 1, lldb::eEncodingSint,
                                   lldb::eFormatChar),
            ivar.size);
      auto *field_type = GetClikeType(ivar_type.GetOpaqueQualType());
      builder.AddField(*iface, builder.GetIdentifier(ivar.name), field_type,
                       byte_offset);
    }
    builder.SetRecordComplete(*iface);
    previous_iface = iface;
  }
  return GetCompilerType(previous_iface);
}

CompilerType
TypeSystemClike::GetRuntimeCompletedObjCType(clike_typesystem::Type *t,
                                           const ExecutionContext *exe_ctx) {
  // NOTE: every caller of this (the four GetNumChildrenImpl/
  // GetChildCompilerTypeAtIndexImpl/GetIndexOfChildWithNameImpl/
  // GetIndexOfChildMemberWithNameImpl query methods) already holds this
  // instance's lock when it calls in here, and CreateRuntimeObjCInterface
  // below now does its runtime/process interaction (the part that can
  // transitively JIT-compile and run a utility-function expression) before
  // taking any TypeSystemClike lock -- see the comment there. That phase
  // still runs nested under *this* call's already-held lock, though: unlike
  // CreateRuntimeObjCInterface's own top-level callers (e.g.
  // ClikeExpressionDeclMap::LookupType), none of these four query methods
  // have been restructured to release their lock first. No test currently
  // exercises that combination (TestExpressionInSyscall/TestTemplateArgs,
  // the two regressions the "Lock every TypeSystemClike..." revert named,
  // both go through the unlocked LookupType path), but it is the same class
  // of hazard, deliberately left unaddressed here to match the scope the
  // revert itself judged separate ("its own change with its own behavioural
  // risk").
  auto *objc = llvm::dyn_cast_or_null<clike_typesystem::ObjCInterfaceType>(t);
  if (!objc)
    return CompilerType();
  // A runtime-built scratch type is already authoritative; asking the runtime
  // again would just rebuild an identical copy.
  if (llvm::is_contained(llvm::make_second_range(m_runtime_objc_types), objc))
    return CompilerType();
  // Prefer the caller's execution context, but fall back to the most recent
  // process seen through any exe_ctx-carrying query (see m_last_seen_process_wp)
  // -- the SBFrame::GetValueForVariablePath name-lookup path calls
  // GetIndexOfChildMemberWithName with no exe_ctx at all, yet must still resolve
  // a hidden ivar reconstructed from the runtime.
  Process *process = exe_ctx ? exe_ctx->GetProcessPtr() : nullptr;
  lldb::ProcessSP process_sp;
  if (process)
    m_last_seen_process_wp = process->shared_from_this();
  else {
    process_sp = m_last_seen_process_wp.lock();
    process = process_sp.get();
  }
  if (!process)
    return CompilerType();
  ObjCLanguageRuntime *runtime = ObjCLanguageRuntime::Get(*process);
  if (!runtime)
    return CompilerType();
  Target &target = process->GetTarget();
  auto scratch_or =
      target.GetScratchTypeSystemForLanguage(lldb::eLanguageTypeObjC_plus_plus);
  if (!scratch_or) {
    llvm::consumeError(scratch_or.takeError());
    return CompilerType();
  }
  auto *scratch = llvm::dyn_cast_or_null<TypeSystemClike>(scratch_or->get());
  // Never redirect within the scratch context itself (that would recurse), and
  // require a distinct scratch context to hold the runtime data.
  if (!scratch || scratch == this)
    return CompilerType();
  ConstString class_name(objc->GetName().GetName());
  if (!class_name)
    return CompilerType();
  CompilerType runtime_ct =
      scratch->CreateRuntimeObjCInterface(class_name, *process, *runtime);
  if (!runtime_ct)
    return CompilerType();
  // Some (or all) of a class's ivars may live outside the debug info this
  // module type was completed from -- e.g. ivars added in a class extension
  // defined in a different image/CU than the one that produced this stub, so
  // no same-module or same-debug-map DWARF search finds them (see
  // DWARFASTParserClike::ParseStructureType). Only prefer the runtime's answer
  // when it actually knows about more ivars than debug info did; otherwise
  // keep answering from the (potentially richer, e.g. better-typed) debug-info
  // fields directly.
  SharedLockedType scratch_t =
      scratch->GetTypeForRead(runtime_ct.GetOpaqueQualType());
  auto *runtime_iface =
      llvm::cast<clike_typesystem::ObjCInterfaceType>(scratch_t.get());
  if (runtime_iface->GetNumFields() <= objc->GetNumFields())
    return CompilerType();
  return runtime_ct;
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

llvm::Expected<uint32_t> TypeSystemClike::GetNumChildrenImpl(
    clike_typesystem::Type *type, bool omit_empty_base_classes,
    const ExecutionContext *exe_ctx) {
  if (!type)
    return 0;
  clike_typesystem::Type *t = Desugar(type);
  // An array's children are its elements.
  if (auto *array = llvm::dyn_cast<clike_typesystem::ArrayType>(t)) {
    if (std::optional<uint64_t> n = array->GetNumElements())
      return *n;
    // No static bound: this may be a variable-length array whose length is only
    // known at runtime. Ask the symbol file to resolve it for this frame.
    if (exe_ctx && array->GetDIEUID() != LLDB_INVALID_UID)
      if (SymbolFile *sym_file = GetSymbolFile())
        if (std::optional<SymbolFile::ArrayInfo> info =
                sym_file->GetDynamicArrayInfoForUID(array->GetDIEUID(), exe_ctx))
          if (!info->element_orders.empty())
            return info->element_orders.back().value_or(0);
    return 0;
  }
  // A pointer/reference is transparent when its children are the pointee's
  // members (see Type::GetTransparentChildPointee); otherwise its single child
  // is the dereferenced value -- except for `void *`, which has no pointee to
  // show and therefore no children.
  if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(t)) {
    if (clike_typesystem::Type *pointee = ptr->GetTransparentChildPointee())
      return GetNumChildrenImpl(pointee, omit_empty_base_classes, exe_ctx);
    // `void *` is the exception: there is nothing to show behind it, so it
    // gets no deref child either.
    return clike_typesystem::IsVoid(ptr->GetPointeeType()) ? 0 : 1;
  }
  if (auto *ref = llvm::dyn_cast<clike_typesystem::ReferenceType>(t)) {
    if (clike_typesystem::Type *pointee = ref->GetTransparentChildPointee())
      return GetNumChildrenImpl(pointee, omit_empty_base_classes, exe_ctx);
    return 1;
  }
  if (!t->IsAggregate())
    return 0;
  // An ObjC interface with no debug-info ivars is completed from the runtime
  // (into the scratch context); answer from that completed type.
  if (CompilerType rt = GetRuntimeCompletedObjCType(t, exe_ctx))
    return rt.GetNumChildren(omit_empty_base_classes, exe_ctx);
  CompleteTypeAssumingWriteLocked(type);
  // A record/enum with no debug info defining it anywhere (e.g.
  // -flimit-debug-info hid its only definition, or a forward-declared
  // `struct Opaque;` that is never defined at all) stays incomplete even
  // after GetCompleteType, since TypeSystemClike deliberately does not
  // force-complete it as an empty definition the way TypeSystemClang does
  // (see GetCompleteType's comment). TypeSystemClang's completion source
  // always leaves a Record reporting *some* field count (0, if it could only
  // force-complete an empty definition) rather than erroring at this level --
  // GetCompleteRecordType never returns null for a Record, so its GetNumChildren
  // Record case practically never takes its "incomplete" branch. Match that by
  // reporting zero children (not an error) here; a pointer/reference to such a
  // type still surfaces the error through GetChildCompilerTypeAtIndex's pointer
  // branch instead (see TestValueObjectErrors), which is the scenario the
  // error string was actually meant for.
  if (!t->IsComplete())
    return 0;
  // Children of a record are its direct base classes followed by its fields.
  // Empty base classes (no data members, recursively) are omitted when
  // requested, matching TypeSystemClang.
  uint32_t num_bases = t->GetNumBaseClasses();
  if (omit_empty_base_classes) {
    auto complete = [this](clike_typesystem::Type *bt) {
      CompleteTypeAssumingWriteLocked(bt);
    };
    uint32_t non_empty_bases = 0;
    for (uint32_t i = 0; i < num_bases; ++i) {
      const clike_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
      if (base && clike_typesystem::RecordType::HasFields(&base->type.Get(), complete))
        ++non_empty_bases;
    }
    num_bases = non_empty_bases;
  }
  return num_bases + t->GetNumFields();
}

BasicType TypeSystemClike::GetBasicTypeEnumeration(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return eBasicTypeInvalid;
  auto *builtin =
      llvm::dyn_cast<clike_typesystem::BuiltinType>(Desugar(t.get()));
  return builtin ? builtin->GetBasicTypeEnumeration() : eBasicTypeInvalid;
}

bool TypeSystemClike::IsPromotableIntegerType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  return t && IsPromotableIntegerTypeImpl(t.get());
}

bool TypeSystemClike::IsPromotableIntegerTypeImpl(
    const clike_typesystem::Type *t) {
  // Follows C++ [conv.prom]: integer types with a conversion rank less than
  // int, plus bool/character/unscoped-enum types, promote to int/unsigned int.
  t = Desugar(t);
  if (auto *builtin = llvm::dyn_cast<clike_typesystem::BuiltinType>(t))
    return builtin->IsPromotableInteger();
  // Unscoped enumerations also promote.
  if (auto *enum_type = llvm::dyn_cast<clike_typesystem::EnumType>(t))
    return !enum_type->IsScoped();
  return false;
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
  if (auto *enum_type = llvm::dyn_cast<clike_typesystem::EnumType>(
          Desugar(tt.get()))) {
    bool needs_unsigned = false;
    if (int_size) {
      const uint64_t int_bits = *int_size * 8;
      // int can represent [-2^(n-1), 2^(n-1) - 1].
      const int64_t int_min = -(int64_t(1) << (int_bits - 1));
      const uint64_t int_max = (uint64_t(1) << (int_bits - 1)) - 1;
      const bool enum_signed = enum_type->IsSigned();
      for (const clike_typesystem::Enumerator &e : enum_type->GetEnumerators()) {
        if (enum_signed) {
          int64_t v = static_cast<int64_t>(e.value);
          if (v < int_min || (v >= 0 && static_cast<uint64_t>(v) > int_max)) {
            needs_unsigned = true;
            break;
          }
        } else if (e.value > int_max) {
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
  // A pointer to an ObjC interface answers field queries as the interface
  // itself would (an ObjC object is only ever accessed through a pointer).
  return GetObjCBaseClassBearingType(t.get())->GetNumFields();
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
  const Field *field = GetObjCBaseClassBearingType(t.get())->GetFieldAtIndex(idx);
  if (!field)
    return CompilerType();
  name = field->name.GetName().str();
  if (bit_offset_ptr)
    *bit_offset_ptr = field->byte_offset * 8 + field->bitfield_bit_offset;
  if (bitfield_bit_size_ptr)
    *bitfield_bit_size_ptr = field->bitfield_bit_size;
  if (is_bitfield_ptr)
    *is_bitfield_ptr = field->IsBitfield();
  return GetCompilerType(&field->type.Get());
}

CompilerDecl TypeSystemClike::GetStaticFieldWithName(opaque_compiler_type_t type,
                                                   llvm::StringRef name) {
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return CompilerDecl();
  CompleteTypeAssumingWriteLocked(tt.get());
  auto *record =
      llvm::dyn_cast<clike_typesystem::RecordType>(Desugar(tt.get()));
  if (!record)
    return CompilerDecl();
  for (uint32_t i = 0, n = record->GetNumStaticDataMembers(); i < n; ++i) {
    const clike_typesystem::StaticDataMember *member =
        record->GetStaticDataMemberAtIndex(i);
    if (member->name.GetName() == name)
      // The opaque decl wraps a clike_typesystem::Decl; the Decl* query
      // methods (DeclGetName / GetTypeForDecl / DeclGetConstantValue) interpret
      // it.
      return CompilerDecl(this, const_cast<clike_typesystem::Decl *>(
                                    m_context.GetOrCreateDecl(member)));
  }
  return CompilerDecl();
}

clike_typesystem::Type *
TypeSystemClike::GetObjCBaseClassBearingType(clike_typesystem::Type *type) {
  clike_typesystem::Type *t = Desugar(type);
  // An Objective-C object is always handled through a pointer (`Foo *`), so a
  // pointer to an ObjC interface answers base-class queries as the interface
  // `Foo` itself would. This does not apply to ordinary C++ pointers.
  if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(t)) {
    clike_typesystem::Type *pointee = ptr->GetPointeeType();
    if (llvm::isa<clike_typesystem::ObjCInterfaceType>(pointee->Desugar())) {
      CompleteTypeAssumingWriteLocked(pointee);
      return pointee->Desugar();
    }
  }
  return t;
}

uint32_t TypeSystemClike::GetNumDirectBaseClasses(opaque_compiler_type_t type) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return 0;
  CompleteTypeAssumingWriteLocked(t.get());
  return GetObjCBaseClassBearingType(t.get())->GetNumBaseClasses();
}

uint32_t TypeSystemClike::GetNumVirtualBaseClasses(opaque_compiler_type_t type) {
  return 0;
}

bool TypeSystemClike::IsAnonymousType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  // An anonymous struct/union (an unnamed record embedded as an unnamed member
  // of its parent) is marked as such by the DWARF parser. Look through sugar,
  // matching TypeSystemClang's RemoveWrappingTypes.
  if (auto *record =
          llvm::dyn_cast<clike_typesystem::RecordType>(Desugar(t.get())))
    return record->IsAnonymousStructOrUnion();
  return false;
}

CompilerType
TypeSystemClike::GetDirectBaseClassAtIndex(opaque_compiler_type_t type,
                                         size_t idx, uint32_t *bit_offset_ptr) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  CompleteTypeAssumingWriteLocked(t.get());
  const clike_typesystem::BaseClass *base =
      GetObjCBaseClassBearingType(t.get())->GetBaseClassAtIndex(idx);
  if (!base)
    return CompilerType();
  if (bit_offset_ptr)
    *bit_offset_ptr = base->byte_offset * 8;
  return GetCompilerType(&base->type.Get());
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
    clike_typesystem::Type *type, ExecutionContext *exe_ctx, size_t idx,
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
  clike_typesystem::Type *t = Desugar(type);

  // Array elements: child N is the element at offset N * element_size.
  if (auto *array = llvm::dyn_cast<clike_typesystem::ArrayType>(t)) {
    if (!ignore_array_bounds) {
      std::optional<uint64_t> num_elements = array->GetNumElements();
      if (num_elements && idx >= *num_elements)
        return CompilerType();
    }
    clike_typesystem::Type *element_type = array->GetElementType();
    child_name = llvm::formatv("[{0}]", idx).str();
    if (std::optional<uint64_t> byte_size = element_type->GetByteSize()) {
      child_byte_size = *byte_size;
      child_byte_offset = idx * *byte_size;
    }
    return GetCompilerType(element_type);
  }

  // A pointer is transparent, mirroring TypeSystemClang (and the reference
  // case below): when asked transparently, expanding it splices in the pointee
  // aggregate's members instead of yielding a single deref child. Only an
  // already-complete aggregate is expanded transparently so that merely
  // inspecting a pointer doesn't force completion of an otherwise-lazy pointee;
  // otherwise child 0 is the dereferenced value (which the DIL `ptr->member`
  // path relies on).
  if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(t)) {
    clike_typesystem::Type *pointee = ptr->GetPointeeType();
    if (clike_typesystem::IsVoid(pointee))
      return CompilerType(); // Can't dereference `void *`.

    // A pointer to an ObjC interface, like any other aggregate pointee, is
    // only expanded transparently when explicitly asked (transparent_pointers)
    // -- e.g. `--ptr-depth`/child enumeration -- matching TypeSystemClang's
    // ObjCObjectPointer case. A non-transparent access (idx 0, as used by
    // `*ptr`/GetDereferencedType) must fall through to the "child 0 is the
    // dereferenced value" path below so `*ptr` yields the whole pointee
    // object rather than its first base/ivar.
    //
    // An ObjC interface is transparent even while incomplete (see
    // PointerType::GetTransparentChildPointee), so complete it first --
    // otherwise its ivars aren't there to splice in.
    if (llvm::isa<clike_typesystem::ObjCInterfaceType>(Desugar(pointee)))
      CompleteTypeAssumingWriteLocked(pointee);
    if (transparent_pointers && ptr->GetTransparentChildPointee()) {
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
    clike_typesystem::Type *desugared_pointee = Desugar(pointee);
    // As in GetBitSize, an ObjC class's size may only be known to the
    // runtime (e.g. a tagged-pointer class like NSIndexSet has no
    // DW_AT_byte_size at all), so try that before falling back to the
    // DWARF-recorded size -- otherwise dereferencing such a pointer (e.g. via
    // a synthetic child provider that wants a dereferenced backend object)
    // spuriously reports the pointee as incomplete.
    std::optional<uint64_t> byte_size;
    if (llvm::isa<clike_typesystem::ObjCInterfaceType>(desugared_pointee) &&
        exe_ctx)
      byte_size = GetObjCRuntimeInstanceByteSize(desugared_pointee,
                                                 exe_ctx->GetProcessPtr());
    if (!byte_size)
      byte_size = pointee->GetByteSize();
    if (!byte_size)
      return llvm::createStringError(
          "incomplete type \"" +
          GetDisplayTypeNameAssumingWriteLocked(pointee).GetString() + "\"");
    child_byte_size = *byte_size;
    child_byte_offset = 0;
    return GetCompilerType(pointee);
  }

  // A reference is transparent, just like a pointer: expanding it either shows
  // the referenced aggregate's members or the single referenced value.
  if (auto *ref = llvm::dyn_cast<clike_typesystem::ReferenceType>(t)) {
    clike_typesystem::Type *pointee = ref->GetPointeeType();

    // As with pointers, only expand an already-complete referent transparently
    // so that merely inspecting the reference doesn't force its completion.
    if (transparent_pointers && pointee->IsAggregate() &&
        pointee->IsComplete()) {
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
      child_name.assign(1, '&');
      child_name += parent_name;
    }
    if (idx != 0)
      return CompilerType();
    // As for pointers, materializing the referent forces its completion.
    CompleteTypeAssumingWriteLocked(pointee);
    if (std::optional<uint64_t> byte_size = pointee->GetByteSize())
      child_byte_size = *byte_size;
    child_byte_offset = 0;
    return GetCompilerType(pointee);
  }

  // An ObjC interface with no debug-info ivars is completed from the runtime
  // (into the scratch context); resolve children against that completed type.
  if (CompilerType rt = GetRuntimeCompletedObjCType(t, exe_ctx))
    return rt.GetChildCompilerTypeAtIndex(
        exe_ctx, idx, transparent_pointers, omit_empty_base_classes,
        ignore_array_bounds, child_name, child_byte_size, child_byte_offset,
        child_bitfield_bit_size, child_bitfield_bit_offset, child_is_base_class,
        child_is_deref_of_parent, valobj, language_flags);

  // Children are laid out as the direct base classes followed by the fields.
  // When omit_empty_base_classes is set, empty base classes (no data members,
  // recursively) are skipped and do not consume a child index, matching
  // TypeSystemClang.
  auto complete = [this](clike_typesystem::Type *bt) {
    CompleteTypeAssumingWriteLocked(bt);
  };
  uint32_t total_bases = t->GetNumBaseClasses();
  uint32_t visible_base_idx = 0;
  for (uint32_t i = 0; i < total_bases; ++i) {
    const clike_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    if (!base)
      continue;
    if (omit_empty_base_classes &&
        !clike_typesystem::RecordType::HasFields(&base->type.Get(), complete))
      continue;
    if (visible_base_idx == idx) {
      // Name the base-class child by its (possibly sugar-wrapped) type name
      // rather than the raw record name: a base recovered for an expression
      // result can be an elaborated/spelling-sugar wrapper whose own m_name is
      // empty (the real name sits on the underlying type), so fall back to the
      // display type name in that case.
      child_name = base->type.Get().GetName().GetName().str();
      if (child_name.empty())
        child_name =
            GetTypeNameAssumingWriteLocked(&base->type.Get(), /*BaseOnly=*/false)
                .GetString();
      child_byte_offset = base->byte_offset;
      // A virtual base has no constant offset: its subobject can sit at
      // different places in different most-derived objects (the diamond case).
      // Read the real offset from the live object's vtable when we can, so e.g.
      // Joiner1.Derived1.VBase and Joiner1.Derived2.VBase resolve to the one
      // shared subobject. Falls back to byte_offset (0) if no live object.
      if (base->is_virtual) {
        auto *derived_rec = llvm::dyn_cast<clike_typesystem::RecordType>(t);
        // ReadVirtualBaseOffset may fall through to
        // ClangASTGenerator::ComputeVBaseOffsetOffset, which builds a
        // throwaway clang AST via the same machinery normal (non-nested)
        // expression generation uses -- including calling back into this
        // instance's own locking GetCompleteType, from what is, from its
        // perspective, a fresh top-level call. We are already inside that
        // lock here (GetChildCompilerTypeAtIndex holds it for this whole
        // call), so briefly release it around this one call: `t`/`base`
        // remain valid either way (base-class arrays are fixed once a
        // record is completed, never appended to afterward), and we
        // re-acquire before touching anything else.
        m_mutex.unlock();
        std::optional<int64_t> vbase_off =
            ReadVirtualBaseOffset(*this, derived_rec, *base, valobj);
        m_mutex.lock();
        if (vbase_off)
          child_byte_offset = *vbase_off;
      }
      if (std::optional<uint64_t> byte_size = base->type.Get().GetByteSize())
        child_byte_size = *byte_size;
      child_is_base_class = true;
      return GetCompilerType(&base->type.Get());
    }
    ++visible_base_idx;
  }

  const Field *field = t->GetFieldAtIndex(idx - visible_base_idx);
  if (!field)
    return CompilerType();

  child_name = field->name.GetName().str();
  child_byte_offset = field->byte_offset;
  // An Objective-C ivar's byte offset is not reliably encoded in DWARF (the
  // compiler emits 0 for every ivar); the authoritative offset lives in the
  // ObjC runtime's `OBJC_IVAR_$_Class.ivar` symbols. Resolve it against the
  // live process when possible, falling back to the DWARF offset otherwise.
  if (llvm::isa<clike_typesystem::ObjCInterfaceType>(t) && exe_ctx) {
    if (Process *process = exe_ctx->GetProcessPtr()) {
      if (ObjCLanguageRuntime *objc_runtime =
              ObjCLanguageRuntime::Get(*process)) {
        CompilerType parent_type = GetCompilerType(t);
        size_t ivar_offset = objc_runtime->GetByteOffsetForIvar(
            parent_type, field->name.GetName().str().c_str());
        if (ivar_offset != static_cast<size_t>(LLDB_INVALID_IVAR_OFFSET))
          child_byte_offset = ivar_offset;
      }
    }
  }
  if (std::optional<uint64_t> byte_size = field->type.Get().GetByteSize())
    child_byte_size = *byte_size;
  if (field->IsBitfield()) {
    child_bitfield_bit_size = field->bitfield_bit_size;
    child_bitfield_bit_offset = field->bitfield_bit_offset;
  }
  return GetCompilerType(&field->type.Get());
}

llvm::Expected<uint32_t>
TypeSystemClike::GetIndexOfChildWithName(opaque_compiler_type_t type,
                                       llvm::StringRef name,
                                       bool omit_empty_base_classes) {
  LockedType t = GetTypeForWrite(type);
  return GetIndexOfChildWithNameImpl(t.get(), name, omit_empty_base_classes);
}

llvm::Expected<uint32_t> TypeSystemClike::GetIndexOfChildWithNameImpl(
    clike_typesystem::Type *type, llvm::StringRef name,
    bool omit_empty_base_classes) {
  if (!type)
    return llvm::createStringError("invalid type");
  CompleteTypeAssumingWriteLocked(type);
  clike_typesystem::Type *t = Desugar(type);
  // See GetIndexOfChildMemberWithNameImpl: redirect an ObjC interface whose
  // ivars come from the runtime (e.g. a hidden ivar in a stripped image) to the
  // runtime-completed scratch type so a by-name lookup finds it.
  if (CompilerType rt = GetRuntimeCompletedObjCType(t, /*exe_ctx=*/nullptr))
    return rt.GetTypeSystem()->GetIndexOfChildWithName(
        rt.GetOpaqueQualType(), name, omit_empty_base_classes);
  // TypeSystemClang): its named children are the aggregate pointee's members,
  // so forward the lookup. A pointer to a non-aggregate has only its (unnamed)
  // deref child, so no named member is directly addressable.
  if (llvm::isa<clike_typesystem::PointerType, clike_typesystem::ReferenceType>(t)) {
    if (clike_typesystem::Type *pointee = t->GetNamedMemberPointee())
      return GetIndexOfChildWithNameImpl(pointee, name, omit_empty_base_classes);
    return llvm::createStringError(
        "TypeSystemClike::GetIndexOfChildWithName: no such child");
  }
  // Base classes are the first children (empty ones omitted when requested,
  // matching GetChildCompilerTypeAtIndex); match them by their type name.
  auto complete = [this](clike_typesystem::Type *bt) {
    CompleteTypeAssumingWriteLocked(bt);
  };
  uint32_t total_bases = t->GetNumBaseClasses();
  uint32_t visible_base_idx = 0;
  for (uint32_t i = 0; i < total_bases; ++i) {
    const clike_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    if (!base)
      continue;
    if (omit_empty_base_classes &&
        !clike_typesystem::RecordType::HasFields(&base->type.Get(), complete))
      continue;
    if (base->type.Get().GetName().GetName() == name)
      return visible_base_idx;
    // A sugar-wrapped base (see GetChildCompilerTypeAtIndex) has an empty raw
    // name; match it by its display type name instead.
    if (base->type.Get().GetName().GetName().empty() &&
        GetTypeNameAssumingWriteLocked(&base->type.Get(), /*BaseOnly=*/false)
                .GetStringRef() == name)
      return visible_base_idx;
    ++visible_base_idx;
  }
  // Fields follow the base classes.
  for (uint32_t i = 0, e = t->GetNumFields(); i < e; ++i) {
    if (t->GetFieldAtIndex(i)->name.GetName() == name)
      return visible_base_idx + i;
  }
  return llvm::createStringError(
      "TypeSystemClike::GetIndexOfChildWithName: no such child");
}

size_t TypeSystemClike::GetIndexOfChildMemberWithName(
    opaque_compiler_type_t type, llvm::StringRef name,
    bool omit_empty_base_classes, std::vector<uint32_t> &child_indexes) {
  // Lazily completes the type, so this needs the write lock. The record the
  // lookup starts from is allowed to transparently search its anonymous
  // (unnamed union/struct) fields; recursion into base classes is not (see
  // GetIndexOfChildMemberWithNameImpl).
  LockedType t = GetTypeForWrite(type);
  return GetIndexOfChildMemberWithNameImpl(t.get(), name, omit_empty_base_classes,
                                           /*descend_anon_fields=*/true,
                                           child_indexes);
}

size_t TypeSystemClike::GetIndexOfChildMemberWithNameImpl(
    clike_typesystem::Type *type, llvm::StringRef name,
    bool omit_empty_base_classes, bool descend_anon_fields,
    std::vector<uint32_t> &child_indexes) {
  if (!type)
    return 0;
  CompleteTypeAssumingWriteLocked(type);
  clike_typesystem::Type *t = Desugar(type);
  // An ObjC interface whose ivars are not in the debug info (e.g. a hidden ivar
  // declared in a class extension in a stripped image) is completed from the
  // runtime into the scratch context; forward the member lookup to that
  // completed type. This mirrors the child-enumeration paths (GetNumChildren /
  // GetChildCompilerTypeAtIndex), which also redirect -- keeping name->index
  // consistent with the child layout. No exe_ctx is available here (the
  // SBFrame::GetValueForVariablePath path doesn't provide one), so
  // GetRuntimeCompletedObjCType falls back to the last-seen process.
  if (CompilerType rt = GetRuntimeCompletedObjCType(t, /*exe_ctx=*/nullptr))
    return rt.GetTypeSystem()->GetIndexOfChildMemberWithName(
        rt.GetOpaqueQualType(), name, omit_empty_base_classes, child_indexes);
  // so that e.g. `ptr->member` / `ref.member` resolves against the pointed-to
  // record. A pointer is now transparent (see GetNumChildren): its children are
  // the pointee aggregate's members directly, with no intervening deref child,
  // so recurse without pushing an index-0 deref step -- matching the reference
  // case and keeping the returned indices consistent with the child layout.
  if (llvm::isa<clike_typesystem::PointerType, clike_typesystem::ReferenceType>(t)) {
    clike_typesystem::Type *pointee = t->GetNamedMemberPointee();
    if (!pointee)
      return 0;
    return GetIndexOfChildMemberWithNameImpl(
        pointee, name, omit_empty_base_classes, /*descend_anon_fields=*/true,
        child_indexes);
  }
  // Compute the number of visible base classes (empty ones omitted when
  // requested), since fields are laid out after them and their child indices
  // must match GetChildCompilerTypeAtIndex.
  auto complete = [this](clike_typesystem::Type *bt) {
    CompleteTypeAssumingWriteLocked(bt);
  };
  uint32_t total_bases = t->GetNumBaseClasses();
  auto base_is_visible = [&](const clike_typesystem::BaseClass *base) {
    return base && (!omit_empty_base_classes ||
                    clike_typesystem::RecordType::HasFields(&base->type.Get(), complete));
  };
  uint32_t num_visible_bases = 0;
  for (uint32_t i = 0; i < total_bases; ++i)
    if (base_is_visible(t->GetBaseClassAtIndex(i)))
      ++num_visible_bases;

  // A matching field is a direct child, laid out after the base classes. An
  // unnamed field is an anonymous union/struct whose members are reached as if
  // they belonged to this record, so recurse into it -- but only when
  // `descend_anon_fields` is set (i.e. this is the record the lookup started
  // from, not a base class we recursed into).
  for (uint32_t i = 0, e = t->GetNumFields(); i < e; ++i) {
    const Field *field = t->GetFieldAtIndex(i);
    llvm::StringRef field_name = field->name.GetName();
    if (field_name == name) {
      child_indexes.push_back(num_visible_bases + i);
      return child_indexes.size();
    }
    if (descend_anon_fields && field_name.empty()) {
      std::vector<uint32_t> save_indices = child_indexes;
      child_indexes.push_back(num_visible_bases + i);
      // An anonymous field of the starting record still injects the members of
      // *its* anonymous fields, so keep descending transparently through it.
      if (GetIndexOfChildMemberWithNameImpl(
              &field->type.Get(), name, omit_empty_base_classes,
              /*descend_anon_fields=*/true, child_indexes))
        return child_indexes.size();
      child_indexes = std::move(save_indices);
    }
  }

  // Otherwise the member may be inherited from a base class. Base classes are
  // the first children, so their child index is their visible position. When
  // recursing into a base we must NOT descend into that base's anonymous fields:
  // C++ name lookup does not find a member that is injected by an anonymous
  // field of a base class (only direct members and further base classes are
  // reachable), matching TypeSystemClang.
  uint32_t visible_base_idx = 0;
  for (uint32_t i = 0; i < total_bases; ++i) {
    const clike_typesystem::BaseClass *base = t->GetBaseClassAtIndex(i);
    if (!base_is_visible(base))
      continue;
    std::vector<uint32_t> save_indices = child_indexes;
    child_indexes.push_back(visible_base_idx);
    if (GetIndexOfChildMemberWithNameImpl(
            &base->type.Get(), name, omit_empty_base_classes,
            /*descend_anon_fields=*/false, child_indexes))
      return child_indexes.size();
    child_indexes = std::move(save_indices);
    ++visible_base_idx;
  }
  return 0;
}

#ifndef NDEBUG
LLVM_DUMP_METHOD void TypeSystemClike::dump(opaque_compiler_type_t type) const {}
#endif

/// Render an enum value as an enumerator name when it matches one exactly. For
/// a "bitfield/flag" enum (every enumerator is a single bit or a union of
/// previously-seen bits) a combined value is decomposed into `A | B`. Mirrors
/// TypeSystemClang's DumpEnumValue.
static bool DumpEnumValue(const clike_typesystem::EnumType &enum_type, Stream &s,
                          const DataExtractor &data, lldb::offset_t byte_offset,
                          size_t byte_size, uint32_t bitfield_bit_offset,
                          uint32_t bitfield_bit_size) {
  lldb::offset_t offset = byte_offset;
  const bool is_signed = enum_type.IsSigned();
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
  const std::vector<clike_typesystem::Enumerator> &enumerators =
      enum_type.GetEnumerators();
  if (enumerators.empty())
    can_be_bitfield = false;
  for (const clike_typesystem::Enumerator &enumerator : enumerators) {
    uint64_t val = enumerator.value;
    if (is_signed)
      val = llvm::SignExtend64(val, 8 * byte_size);
    if (llvm::popcount(val) != 1 && (val & ~covered_bits) != 0)
      can_be_bitfield = false;
    covered_bits |= val;
    ++num_enumerators;
    if (val == enum_svalue) {
      s.PutCString(enumerator.name.GetName());
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
  for (const clike_typesystem::Enumerator &enumerator : enumerators)
    if (enumerator.value)
      values.emplace_back(enumerator.value, enumerator.name.GetName());

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
  const clike_typesystem::Type *t = Desugar(tt.get());
  // Aggregates don't have a scalar value to print; their children are dumped
  // individually.
  if (t->IsAggregate())
    return false;
  if (format == eFormatDefault)
    format = t->GetFormat();
  // Enumerations: show the enumerator name when possible rather than the raw
  // integer value.
  if (auto *enum_type = llvm::dyn_cast<clike_typesystem::EnumType>(t)) {
    if (format == eFormatEnum || format == eFormatDefault)
      return DumpEnumValue(*enum_type, s, data, data_offset, data_byte_size,
                           bitfield_bit_offset, bitfield_bit_size);
  }
  // Some formats dump the value as a sequence of smaller items rather than one
  // scalar: e.g. the char/bytes formats print each byte, and the unicode
  // formats print each code unit. Split the byte size into that many items so
  // e.g. a 16-byte `__uint128_t` printed with a char format is shown as 16
  // characters instead of being rejected as too wide (matching TypeSystemClang).
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
    Stream &s, clike_typesystem::Type *field_type, llvm::StringRef name) {
  using namespace clike_typesystem;
  std::string dims;
  clike_typesystem::Type *cur = field_type;
  while (auto *array = llvm::dyn_cast_or_null<ArrayType>(cur)) {
    if (std::optional<uint64_t> n = array->GetNumElements())
      dims += llvm::formatv("[{0}]", *n).str();
    else
      dims += "[]";
    cur = array->GetElementType();
  }
  std::string base =
      GetTypeNameAssumingWriteLocked(cur, /*BaseOnly=*/false).GetStringRef().str();
  s << base;
  if (!name.empty())
    s << " " << name;
  s << dims;
}

// Append a clang-like member-function declaration ("<ret> <name>(<params>)
// <cv-qualifiers> <ref-qualifier>;") for a record's method, mirroring how
// clang::RecordDecl::print renders a CXXMethodDecl. Reuses BuildFunctionName's
// return-type/parameter rendering.
static void AppendMemberFunctionDecl(Stream &s,
                                     const clike_typesystem::MemberFunction &m) {
  using namespace clike_typesystem;
  if (m.is_static)
    s << "static ";
  FunctionType *fn = llvm::dyn_cast<FunctionType>(&m.type.Get());
  std::string name = m.name.GetName().str();
  if (fn) {
    s << clike_typesystem::BuildFunctionName(fn, name);
  } else {
    s << name << "()";
  }
  if (m.is_const)
    s << " const";
  if (m.is_volatile)
    s << " volatile";
  switch (m.ref_qualifier) {
  case RefQualifier::None:
    break;
  case RefQualifier::LValue:
    s << " &";
    break;
  case RefQualifier::RValue:
    s << " &&";
    break;
  }
  s.PutCString(";\n");
}

void TypeSystemClike::DumpTypeDescription(opaque_compiler_type_t type, Stream &s,
                                        DescriptionLevel level) {
  // Lazily completes the type below, so this needs the write lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return;
  // A typedef is checked before desugaring (unlike the record/enum/ObjC
  // branches below, which want the canonical type): clang's equivalent dump
  // only special-cases a type that IS itself a TypedefType, printing
  // "typedef <name>" and leaving the underlying type alone, matching
  // TypeSystemClang::DumpTypeDescription's `case clang::Type::Typedef`.
  if (llvm::isa<clike_typesystem::TypedefType>(tt.get())) {
    s.PutCString("typedef ");
    s.PutCString(GetTypeNameAssumingWriteLocked(tt.get(), /*BaseOnly=*/true)
                     .GetStringRef());
    return;
  }
  clike_typesystem::Type *t = Desugar(tt.get());

  if (auto *enum_type = llvm::dyn_cast<clike_typesystem::EnumType>(t)) {
    CompleteTypeAssumingWriteLocked(t);
    // Scoped enums (`enum class`) print their tag so the definition matches C++
    // source form. The class/struct distinction is not modeled, so scoped enums
    // always use `class` (clang's default spelling).
    const char *scope = enum_type->IsScoped() ? " class" : "";
    s.Printf("enum%s %s {\n", scope,
             GetTypeNameAssumingWriteLocked(t, /*BaseOnly=*/false).GetCString());
    for (const clike_typesystem::Enumerator &e : enum_type->GetEnumerators()) {
      if (enum_type->IsSigned())
        s.Printf("    %s = %" PRId64 ",\n", e.name.GetName().str().c_str(),
                 static_cast<int64_t>(e.value));
      else
        s.Printf("    %s = %" PRIu64 ",\n", e.name.GetName().str().c_str(),
                 e.value);
    }
    s.PutCString("}");
    return;
  }

  if (auto *iface = llvm::dyn_cast<clike_typesystem::ObjCInterfaceType>(t)) {
    CompleteTypeAssumingWriteLocked(t);
    // An ObjC interface is a RecordType (ivars modeled as fields, superclass
    // as its one base class), but its source-level spelling is `@interface`,
    // not `struct`/`class` -- dump it separately from the generic RecordType
    // branch below (which it would otherwise also match).
    s.Printf("@interface %s",
             GetTypeNameAssumingWriteLocked(t, /*BaseOnly=*/true).GetCString());
    if (const clike_typesystem::BaseClass *super = iface->GetBaseClassAtIndex(0))
      s.Printf(" : %s",
               GetTypeNameAssumingWriteLocked(&super->type.Get(),
                                              /*BaseOnly=*/true)
                   .GetCString());
    s.PutCString(" {\n");
    for (uint32_t i = 0, e = iface->GetNumFields(); i != e; ++i) {
      const clike_typesystem::Field *field = iface->GetFieldAtIndex(i);
      if (!field)
        continue;
      s.PutCString("    ");
      AppendMemberDeclAssumingWriteLocked(s, &field->type.Get(), field->name.GetName());
      s.PutCString(";\n");
    }
    s.PutCString("}\n");
    CompleteMemberFunctionsAssumingWriteLocked(iface);
    for (uint32_t i = 0, e = iface->GetNumObjCMethods(); i != e; ++i) {
      if (const clike_typesystem::ObjCMethod *m = iface->GetObjCMethodAtIndex(i))
        s.Printf("%s;\n", m->name.GetName().str().c_str());
    }
    return;
  }

  if (auto *record = llvm::dyn_cast<clike_typesystem::RecordType>(t)) {
    CompleteTypeAssumingWriteLocked(t);
    // `struct` and `class` both map onto the same ClassType C++ class (the
    // keyword doesn't affect layout); the actual source-spelling keyword is
    // tracked separately via IsClassKeyword(). Consult that instead of the
    // C++ type hierarchy so `struct Foo` prints "struct Foo", not "class Foo".
    const char *tag = record->IsUnion()
                          ? "union"
                          : (record->IsClassKeyword() ? "class" : "struct");
    // This dump context matches clang's printer, which uses the bare
    // (unqualified) tag name here, not the fully-qualified name.
    s.Printf("%s %s {\n", tag,
             GetTypeNameAssumingWriteLocked(t, /*BaseOnly=*/true).GetCString());
    for (uint32_t i = 0, e = record->GetNumFields(); i != e; ++i) {
      const clike_typesystem::Field *field = record->GetFieldAtIndex(i);
      if (!field)
        continue;
      s.PutCString("    ");
      AppendMemberDeclAssumingWriteLocked(s, &field->type.Get(), field->name.GetName());
      if (field->IsBitfield())
        s.Printf(" : %u", field->bitfield_bit_size);
      s.PutCString(";\n");
    }
    CompleteMemberFunctionsAssumingWriteLocked(record);
    for (uint32_t i = 0, e = record->GetNumMemberFunctions(); i != e; ++i) {
      const clike_typesystem::MemberFunction *m =
          record->GetMemberFunctionAtIndex(i);
      if (!m)
        continue;
      s.PutCString("    ");
      AppendMemberFunctionDecl(s, *m);
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
  // An Objective-C class's layout (ivar offsets) is provided by the ObjC
  // runtime rather than being fixed by the debug info.
  SharedLockedType t = GetTypeForRead(type);
  return t && llvm::isa<clike_typesystem::ObjCInterfaceType>(Desugar(t.get()));
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
  return clike_typesystem::CVQualifiedType::GetCVRMask(t.get());
}

std::optional<size_t>
TypeSystemClike::GetTypeBitAlign(opaque_compiler_type_t type,
                              ExecutionContextScope *exe_scope) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return std::nullopt;
  const clike_typesystem::Type *t = Desugar(tt.get());
  return t ? t->GetAlignmentInBits() : std::nullopt;
}

CompilerType TypeSystemClike::GetBuiltinTypeByName(ConstString name) {
  // The `_BitInt`/`unsigned _BitInt` branch below allocates a bespoke tracked
  // type, so this needs the write lock.
  auto write_lock = LockForWrite();
  llvm::StringRef name_ref = name.GetStringRef();

  // `_BitInt(N)` / `unsigned _BitInt(N)` are synthesized on demand (they never
  // appear as an enumerated builtin spelling). Mirror TypeSystemClang: parse the
  // bit width and lay the type out with the target's ABI size.
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
        m_context.GetBuiltinType(clike_typesystem::BuiltinKind::Int128));
  if (name_ref == "__uint128_t")
    return GetCompilerType(
        m_context.GetBuiltinType(clike_typesystem::BuiltinKind::UnsignedInt128));

  if (clike_typesystem::BuiltinType *bt =
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
  // The Objective-C object/class/selector basic types are not builtins here:
  // they are typedefs over a pointer to an opaque runtime record, matching how
  // the DWARF parser and ClangTypeConverter (see ConvertObjCObjectPointer)
  // model them elsewhere in TypeSystemClike.
  switch (basic_type) {
  case eBasicTypeObjCID:
  case eBasicTypeObjCClass: {
    const bool is_class = basic_type == eBasicTypeObjCClass;
    clike_typesystem::Builder builder(*this);
    CompilerType ptr = builder.CreatePointerType(
        clike_typesystem::CreateOpaqueObjCRecordType(
        builder, is_class ? "objc_class" : "objc_object"));
    return builder.CreateTypedefType(is_class ? "Class" : "id", ptr);
  }
  case eBasicTypeObjCSel: {
    clike_typesystem::Builder builder(*this);
    CompilerType ptr = builder.CreatePointerType(
        clike_typesystem::CreateOpaqueObjCRecordType(builder, "objc_selector"));
    return builder.CreateTypedefType("SEL", ptr);
  }
  default:
    break;
  }
  // Everything else the type system models is one of the enumerated builtins.
  // Anything it doesn't (e.g. eBasicTypeHalf) has no basic type here.
  std::optional<clike_typesystem::BuiltinKind> kind =
      clike_typesystem::KnownBuiltinTypes::KindForBasicType(basic_type);
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
  clike_typesystem::Builder builder(*this);
  return builder.CreateFunctionType(builder.GetVoidType(),
                                    /*is_variadic=*/true);
}

CompilerType
TypeSystemClike::GetBuiltinTypeForEncodingAndBitSize(Encoding encoding,
                                                   size_t bit_size) {
  auto read_lock = LockForRead();
  std::optional<clike_typesystem::BuiltinKind> kind =
      clike_typesystem::KnownBuiltinTypes::KindForEncodingAndBitSize(encoding,
                                                                   bit_size);
  if (!kind)
    return CompilerType();
  return GetCompilerType(m_context.GetBuiltinType(*kind));
}

bool TypeSystemClike::IsBeingDefined(opaque_compiler_type_t type) {
  return false;
}

bool TypeSystemClike::IsConst(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  if (auto *cv = llvm::dyn_cast<clike_typesystem::CVQualifiedType>(t.get()))
    return cv->IsConst();
  return false;
}

uint32_t TypeSystemClike::IsHomogeneousAggregate(opaque_compiler_type_t type,
                                               CompilerType *base_type_ptr) {
  // Lazily completes the type, so this needs the write lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return 0;
  auto *record = llvm::dyn_cast_or_null<clike_typesystem::RecordType>(
      Desugar(tt.get()));
  if (!record)
    return 0;
  clike_typesystem::Type *completed = CompleteTypeAssumingWriteLocked(record);
  if (!completed || !completed->IsComplete())
    return 0;
  uint32_t num_fields = 0;
  clike_typesystem::Type *base_type =
      record->GetHomogeneousAggregateBase(num_fields);
  if (!base_type)
    return 0;
  if (base_type_ptr)
    *base_type_ptr = GetCompilerType(base_type);
  return num_fields;
}

bool TypeSystemClike::IsPolymorphicClass(opaque_compiler_type_t type) {
  // A forward-declared/incomplete polymorphic record doesn't get
  // SetRecordPolymorphic applied until DWARF completion runs, so complete
  // the type first (matching the other predicates in this file, e.g.
  // IsTemplateType / GetNumTemplateArguments), otherwise IsPolymorphic()
  // reads the not-yet-populated default of false. This needs the write lock.
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return false;
  clike_typesystem::Type *t = CompleteTypeAssumingWriteLocked(tt.get());
  return t && t->IsPolymorphic();
}

bool TypeSystemClike::IsTypedefType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return false;
  return llvm::isa<clike_typesystem::TypedefType>(
      clike_typesystem::ElaboratedType::Strip(t.get()));
}

CompilerType TypeSystemClike::GetTypedefedType(opaque_compiler_type_t type) {
  SharedLockedType t = GetTypeForRead(type);
  if (!t)
    return CompilerType();
  if (auto *td = llvm::dyn_cast<clike_typesystem::TypedefType>(
          clike_typesystem::ElaboratedType::Strip(t.get())))
    return GetCompilerType(td->GetUnderlyingType());
  return CompilerType();
}

bool TypeSystemClike::IsVectorType(opaque_compiler_type_t type,
                                 CompilerType *element_type, uint64_t *size) {
  SharedLockedType tt = GetTypeForRead(type);
  if (!tt)
    return false;
  const clike_typesystem::Type *t = Desugar(tt.get());
  auto *array = llvm::dyn_cast<clike_typesystem::ArrayType>(t);
  if (!array || !array->IsVector())
    return false;
  if (element_type)
    *element_type = GetCompilerType(array->GetElementType());
  if (size) {
    if (std::optional<uint64_t> num = array->GetNumElements())
      *size = *num;
  }
  return true;
}

CompilerType
TypeSystemClike::GetFullyUnqualifiedType(opaque_compiler_type_t type) {
  // GetFullyUnqualifiedTypeImpl may allocate fresh Pointer/Reference/Array
  // wrapper nodes, so this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  return GetCompilerType(GetFullyUnqualifiedTypeImpl(t.get()));
}

clike_typesystem::Type *
TypeSystemClike::GetFullyUnqualifiedTypeImpl(clike_typesystem::Type *t) {
  // Mirror TypeSystemClang::GetFullyUnqualifiedType: strip top-level
  // cv-qualifiers, and recurse through pointers/references/arrays so their
  // pointee/element is likewise fully unqualified (so `namesp::Virtual * const`
  // -> `namesp::Virtual *`, and `const char *` -> `char *`). Other sugar
  // (typedefs, elaborated spellings) is preserved. A reference has no
  // cv-qualifiers of its own, but its referent is unqualified.
  if (!t)
    return t;
  // Strip any stacked top-level cv-qualifiers.
  while (auto *cv = llvm::dyn_cast<clike_typesystem::CVQualifiedType>(t))
    t = cv->GetUnderlyingType();

  clike_typesystem::Builder builder(*this);
  if (auto *ptr = llvm::dyn_cast<clike_typesystem::PointerType>(t)) {
    clike_typesystem::Type *pointee = ptr->GetPointeeType();
    clike_typesystem::Type *stripped = GetFullyUnqualifiedTypeImpl(pointee);
    if (stripped != pointee)
      return static_cast<clike_typesystem::Type *>(
          builder.CreatePointerType(GetCompilerType(stripped))
              .GetOpaqueQualType());
    return t;
  }
  if (auto *ref = llvm::dyn_cast<clike_typesystem::ReferenceType>(t)) {
    clike_typesystem::Type *pointee = ref->GetPointeeType();
    clike_typesystem::Type *stripped = GetFullyUnqualifiedTypeImpl(pointee);
    if (stripped != pointee)
      return static_cast<clike_typesystem::Type *>(
          builder.CreateReferenceType(GetCompilerType(stripped), ref->IsRValue())
              .GetOpaqueQualType());
    return t;
  }
  if (auto *array = llvm::dyn_cast<clike_typesystem::ArrayType>(t)) {
    clike_typesystem::Type *element = array->GetElementType();
    clike_typesystem::Type *stripped = GetFullyUnqualifiedTypeImpl(element);
    if (stripped != element)
      return static_cast<clike_typesystem::Type *>(
          builder
              .CreateArrayType(GetCompilerType(stripped),
                               array->GetNumElements())
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
  if (auto *ref =
          llvm::dyn_cast<clike_typesystem::ReferenceType>(Desugar(tt.get())))
    return GetCompilerType(ref->GetPointeeType());
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
  auto *ref = llvm::dyn_cast_or_null<clike_typesystem::ReferenceType>(
      tt ? Desugar(tt.get()) : nullptr);
  if (!ref)
    return false;
  if (pointee_type)
    *pointee_type = GetCompilerType(ref->GetPointeeType());
  if (is_rvalue)
    *is_rvalue = ref->IsRValue();
  return true;
}

/// The record backing a class-template instantiation, or null if \p type is not
/// a (possibly sugared) record. Template arguments are populated during
/// completion, so callers must complete the type first.
static clike_typesystem::RecordType *
GetRecordForTemplateArgs(clike_typesystem::Type *type) {
  return llvm::dyn_cast_or_null<clike_typesystem::RecordType>(Desugar(type));
}

bool TypeSystemClike::IsTemplateType(opaque_compiler_type_t type) {
  // Template arguments are only populated once the record is completed, so
  // this needs the write lock.
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return false;
  CompleteTypeAssumingWriteLocked(t.get());
  if (auto *record = GetRecordForTemplateArgs(t.get()))
    return record->GetNumTemplateArguments() > 0;
  return false;
}

size_t TypeSystemClike::GetNumTemplateArguments(opaque_compiler_type_t type,
                                              bool expand_pack) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return 0;
  CompleteTypeAssumingWriteLocked(t.get());
  if (auto *record = GetRecordForTemplateArgs(t.get()))
    return record->GetNumTemplateArguments();
  return 0;
}

lldb::TemplateArgumentKind
TypeSystemClike::GetTemplateArgumentKind(opaque_compiler_type_t type, size_t idx,
                                       bool expand_pack) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return eTemplateArgumentKindNull;
  CompleteTypeAssumingWriteLocked(t.get());
  if (auto *record = GetRecordForTemplateArgs(t.get()))
    if (const clike_typesystem::TemplateArgument *arg =
            record->GetTemplateArgumentAtIndex(idx))
      return arg->kind;
  return eTemplateArgumentKindNull;
}

CompilerType TypeSystemClike::GetTypeTemplateArgument(opaque_compiler_type_t type,
                                                    size_t idx,
                                                    bool expand_pack) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  CompleteTypeAssumingWriteLocked(t.get());
  if (auto *record = GetRecordForTemplateArgs(t.get()))
    if (const clike_typesystem::TemplateArgument *arg =
            record->GetTemplateArgumentAtIndex(idx)) {
      if (arg->kind != lldb::eTemplateArgumentKindType)
        return CompilerType();
      // A type argument always names a type -- a `void` argument (e.g.
      // `coroutine_handle<void>`, which DWARF spells by omitting DW_AT_type)
      // names the `void` builtin, so this is never an invalid CompilerType.
      // That matches TypeSystemClang, whose `void` QualType is always valid.
      return GetCompilerType(&arg->type->Get());
    }
  return CompilerType();
}

std::optional<CompilerType::IntegralTemplateArgument>
TypeSystemClike::GetIntegralTemplateArgument(opaque_compiler_type_t type,
                                           size_t idx, bool expand_pack) {
  LockedType tt = GetTypeForWrite(type);
  if (!tt)
    return std::nullopt;
  CompleteTypeAssumingWriteLocked(tt.get());
  auto *record = GetRecordForTemplateArgs(tt.get());
  if (!record)
    return std::nullopt;
  const clike_typesystem::TemplateArgument *arg =
      record->GetTemplateArgumentAtIndex(idx);
  if (!arg || arg->kind != eTemplateArgumentKindIntegral)
    return std::nullopt;
  // A pointer/reference-typed argument (e.g. `&temp1.member`) has no integral
  // value; report it as absent rather than a bogus scalar.
  if (arg->type)
    if (llvm::isa<clike_typesystem::PointerType>(&arg->type->Get()) ||
        llvm::isa<clike_typesystem::ReferenceType>(&arg->type->Get()))
      return std::nullopt;

  // Reconstruct the value with the argument type's signedness.
  Scalar value;
  if (arg->type && arg->type->Get().GetEncoding() == eEncodingSint)
    value = static_cast<int64_t>(arg->integral_value);
  else
    value = arg->integral_value;
  return CompilerType::IntegralTemplateArgument{
      value, GetCompilerType(arg->type ? &arg->type->Get() : nullptr)};
}

CompilerType
TypeSystemClike::GetDirectNestedTypeWithName(opaque_compiler_type_t type,
                                           llvm::StringRef name) {
  LockedType t = GetTypeForWrite(type);
  if (!t)
    return CompilerType();
  CompleteTypeAssumingWriteLocked(t.get());
  if (auto *record = GetRecordForTemplateArgs(t.get()))
    if (clike_typesystem::Type *nested = record->GetNestedTypeWithName(name))
      return GetCompilerType(nested);
  return CompilerType();
}
