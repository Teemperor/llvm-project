//===-- TypeSystemClike.h -------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPESYSTEMCLIKE_H
#define LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPESYSTEMCLIKE_H

#include "lldb/Symbol/CompilerType.h"
#include "lldb/Symbol/TypeSystem.h"
#include "lldb/Utility/Locked.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/RWMutex.h"
#include "llvm/TargetParser/Triple.h"

#include "Builder.h"
#include "Context.h"

#include <memory>
#include <mutex>
#include <shared_mutex>

namespace lldb_private {

/// A TypeSystem for C-like languages such as C, C++ and Objective-C.
class TypeSystemClike : public TypeSystem {
  // LLVM RTTI support
  static char ID;

public:
  /// \p opts must already have been derived from a real target; use Create
  /// below rather than deriving them at the call site.
  TypeSystemClike(llvm::StringRef name, clike::LanguageOpts opts);
  ~TypeSystemClike() override;

  /// The type system for \p triple, or an error if language options cannot be
  /// derived for that target (see clike::LanguageOpts::Create).
  static llvm::Expected<lldb::TypeSystemSP> Create(llvm::StringRef name,
                                                   llvm::Triple triple);

  /// Wrap one of our own Type nodes into a CompilerType owned by this system.
  CompilerType GetCompilerType(clike::Type *type);
  /// A CompilerType's opaque pointer isn't itself const-tracked -- constness
  /// here is only a lock-scope safety net inside this class's own query
  /// methods (see the locking note below), not a property of the Type graph.
  /// Wrapping a `const Type *` obtained from a read lock is safe: it neither
  /// mutates the pointee now nor grants the caller a way to later, since any
  /// subsequent mutation still has to go through a public TypeSystemClike
  /// method, which takes its own lock.
  CompilerType GetCompilerType(const clike::Type *type) {
    return GetCompilerType(const_cast<clike::Type *>(type));
  }

  /// Recover the Type node backing a CompilerType created by this system,
  /// WITHOUT taking this instance's lock. This is intentionally unlocked: it
  /// exists for callers that are either (a) not part of this class's own
  /// query API (the expression-parser plugins and clike::Builder,
  /// which run under their own, separate single-threading guarantees -- see
  /// the class-level locking note below), or (b) already holding this
  /// instance's lock themselves. New code inside TypeSystemClike.cpp must NOT
  /// call this directly -- use GetTypeForRead/GetTypeForWrite instead, so the
  /// lock can never be forgotten.
  static clike::Type *GetClikeType(lldb::opaque_compiler_type_t type) {
    return static_cast<clike::Type *>(type);
  }

  /// Locking. TypeSystemClike protects its Type/Context state with a single
  /// reader/writer lock -- but a query is not confined to one instance. A type
  /// may reference a type another instance owns (a `TypeRef` names a node, and
  /// the node records its owner -- see clike::Type::
  /// GetOwningContext), and every query method walks such references freely:
  /// through a pointee, an element, a base class, a field, a template
  /// argument. Two things create those references:
  ///
  ///   - `-gmodules` debug info, where each `.pcm` is its own Module with its
  ///     own TypeSystemClike: a record in one module has a field, base class,
  ///     typedef target or Objective-C superclass owned by another.
  ///   - the expression evaluator and data formatters, which build types in
  ///     the target's *scratch* instance around types the module that parsed
  ///     them still owns (see ClangTypeConverter).
  ///
  /// So a query has to hold the lock of every instance it might reach, and it
  /// has to take them all up front: acquiring them one at a time as the walk
  /// arrives would let two queries over overlapping sets each end up holding
  /// what the other is waiting for. GetLockOrder() answers which instances
  /// those are (see there), and LockSet takes them in ascending instance
  /// address -- one global order, which is what makes any two sets safe to
  /// acquire however they overlap.
  ///
  /// GetTypeForWrite/GetTypeForRead below are the ONLY sanctioned way for a
  /// query method to turn an opaque_compiler_type_t into a Type* -- the
  /// returned RAII object bundles the locks together with the pointer so they
  /// can't be dropped while the pointer is still in use, and a read lock only
  /// ever hands out a `const Type *`, so it is impossible to mutate through it
  /// (mutation happens only through clike::Builder, which requires
  /// a non-const Type* / TypeSystemClike&).
  ///
  /// The locks are intentionally non-recursive (see llvm::sys::RWMutex): every
  /// public method that needs them takes them exactly once, at its own entry
  /// point, and delegates to a private "*Impl" (or, for the completion family,
  /// "*AssumingWriteLocked") method that assumes they are already held and
  /// never re-locks -- including when it recurses onto a *different* Type*
  /// reachable from the first, whether that type belongs to this instance or
  /// to another one in the set. That extends to calling *another*
  /// TypeSystemClike: an entry point there would try to re-take a lock this
  /// thread already holds, so those calls go to the non-locking entry points
  /// too.
  ///
  /// @{

  /// A lock held on a whole set of TypeSystemClike instances at once, taken in
  /// ascending instance address so that overlapping sets can never deadlock.
  /// Released in reverse order.
  class LockSet {
  public:
    LockSet() = default;
    /// Locks every instance in \p ordered, which MUST already be sorted by
    /// ascending address and free of duplicates (GetLockOrder guarantees
    /// both). \p exclusive selects a writer or a reader lock; the same choice
    /// applies to every instance in the set.
    LockSet(llvm::ArrayRef<TypeSystemClike *> ordered, bool exclusive);
    ~LockSet() { Release(); }

    LockSet(LockSet &&other)
        : m_locked(std::move(other.m_locked)), m_exclusive(other.m_exclusive) {
      other.m_locked.clear();
    }
    LockSet &operator=(LockSet &&other) {
      if (this != &other) {
        Release();
        m_locked = std::move(other.m_locked);
        m_exclusive = other.m_exclusive;
        other.m_locked.clear();
      }
      return *this;
    }
    LockSet(const LockSet &) = delete;
    LockSet &operator=(const LockSet &) = delete;

    /// Whether this lock covers \p ts. Used to assert that a query never
    /// reaches an instance it did not lock -- the check that would otherwise
    /// only show up as a rare data race.
    bool Covers(const TypeSystemClike *ts) const {
      return llvm::is_contained(m_locked, ts);
    }

  private:
    void Release();

    /// The instances actually locked, in acquisition (ascending address)
    /// order. Almost always exactly one, so this stays on the stack.
    llvm::SmallVector<TypeSystemClike *, 4> m_locked;
    bool m_exclusive = false;
  };

  /// A pointer borrowed under a LockSet: the lock lives exactly as long as the
  /// borrow. Move-only -- a borrow is always confined to the one query method
  /// that took it.
  template <typename PtrT> class LockedIn {
  public:
    LockedIn() = default;
    LockedIn(LockSet locks, PtrT ptr) : m_locks(std::move(locks)), m_ptr(ptr) {}

    LockedIn(LockedIn &&) = default;
    LockedIn &operator=(LockedIn &&) = default;
    LockedIn(const LockedIn &) = delete;
    LockedIn &operator=(const LockedIn &) = delete;

    PtrT operator->() const { return m_ptr; }
    decltype(auto) operator*() const { return *m_ptr; }
    PtrT get() const { return m_ptr; }
    explicit operator bool() const { return m_ptr != nullptr; }

    /// The locks held for this borrow, for asserting coverage as the query
    /// walks onto types owned by other instances.
    const LockSet &GetLocks() const { return m_locks; }

  private:
    LockSet m_locks;
    PtrT m_ptr = nullptr;
  };

  using LockedType = LockedIn<clike::Type *>;
  using SharedLockedType = LockedIn<const clike::Type *>;

  /// The ONLY sanctioned way to get a mutable Type* out of an opaque compiler
  /// type. Do not let the raw pointer outlive the returned LockedType.
  LockedType GetTypeForWrite(lldb::opaque_compiler_type_t type) {
    return LockedType(LockSet(GetLockOrder(), /*exclusive=*/true),
                      GetClikeType(type));
  }
  /// The ONLY sanctioned way to get a read-only Type* out of an opaque
  /// compiler type. The pointer is `const`, so it cannot be used to mutate.
  SharedLockedType GetTypeForRead(lldb::opaque_compiler_type_t type) const {
    return SharedLockedType(LockSet(GetLockOrder(), /*exclusive=*/false),
                            GetClikeType(type));
  }

  /// The instances a query rooted at this one may touch, sorted by ascending
  /// address and deduplicated -- i.e. exactly the set LockSet must acquire,
  /// already in the global lock order. Always includes `this`.
  ///
  /// This has to be knowable *before* any lock is taken, so it is derived from
  /// structure that is fixed up front rather than from references observed so
  /// far: a query that is the first to create a cross-instance reference would
  /// otherwise walk into an instance it never locked. See ComputeLockOrder.
  llvm::SmallVector<TypeSystemClike *, 4> GetLockOrder() const;
  /// @}

  /// The target triple this type system was created for (used e.g. to build a
  /// throwaway Clang AST for ABI/vtable-layout queries).
  const llvm::Triple &GetTriple() const { return m_triple; }

  // Plugin lifecycle
  static void Initialize();
  static void Terminate();

  static llvm::StringRef GetPluginNameStatic() { return "cpp"; }

  static lldb::TypeSystemSP CreateInstance(lldb::LanguageType language,
                                           Module *module, Target *target);

  static LanguageSet GetSupportedLanguagesForTypes();
  static LanguageSet GetSupportedLanguagesForExpressions();

  // PluginInterface
  llvm::StringRef GetPluginName() override { return GetPluginNameStatic(); }

  // LLVM RTTI support
  bool isA(const void *ClassID) const override { return ClassID == &ID; }
  static bool classof(const TypeSystem *ts) { return ts->isA(&ID); }

  // CompilerDecl functions
  ConstString DeclGetName(void *opaque_decl) override;
  CompilerType GetTypeForDecl(void *opaque_decl) override;

  // CompilerDeclContext functions
  ConstString DeclContextGetName(void *opaque_decl_ctx) override;
  ConstString DeclContextGetScopeQualifiedName(void *opaque_decl_ctx) override;
  std::vector<lldb_private::CompilerContext>
  DeclContextGetCompilerContext(void *opaque_decl_ctx) override;
  bool DeclContextIsClassMethod(void *opaque_decl_ctx) override;
  bool DeclContextIsContainedInLookup(void *opaque_decl_ctx,
                                      void *other_opaque_decl_ctx) override;
  lldb::LanguageType DeclContextGetLanguage(void *opaque_decl_ctx) override;

  // Tests
#ifndef NDEBUG
  bool Verify(lldb::opaque_compiler_type_t type) override;
#endif

  bool IsArrayType(lldb::opaque_compiler_type_t type,
                   CompilerType *element_type, uint64_t *size,
                   bool *is_incomplete) override;
  bool IsAggregateType(lldb::opaque_compiler_type_t type) override;
  bool IsAnonymousType(lldb::opaque_compiler_type_t type) override;
  bool IsCharType(lldb::opaque_compiler_type_t type) override;
  bool IsCompleteType(lldb::opaque_compiler_type_t type) override;
  bool IsDefined(lldb::opaque_compiler_type_t type) override;
  bool IsFloatingPointType(lldb::opaque_compiler_type_t type) override;
  bool IsFunctionType(lldb::opaque_compiler_type_t type) override;
  size_t
  GetNumberOfFunctionArguments(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFunctionArgumentAtIndex(lldb::opaque_compiler_type_t type,
                                          const size_t index) override;
  bool IsFunctionPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsMemberFunctionPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsMemberDataPointerType(lldb::opaque_compiler_type_t type) override;
  bool IsBlockPointerType(lldb::opaque_compiler_type_t type,
                          CompilerType *function_pointer_type_ptr) override;
  bool IsIntegerType(lldb::opaque_compiler_type_t type,
                     bool &is_signed) override;
  bool IsScopedEnumerationType(lldb::opaque_compiler_type_t type) override;
  bool IsEnumerationType(lldb::opaque_compiler_type_t type,
                         bool &is_signed) override;
  bool IsPossibleDynamicType(lldb::opaque_compiler_type_t type,
                             CompilerType *target_type, bool check_cplusplus,
                             bool check_objc) override;
  bool IsPointerType(lldb::opaque_compiler_type_t type,
                     CompilerType *pointee_type) override;
  bool IsScalarType(lldb::opaque_compiler_type_t type) override;
  bool IsVoidType(lldb::opaque_compiler_type_t type) override;
  bool CanPassInRegisters(const CompilerType &type) override;
  bool SupportsLanguage(lldb::LanguageType language) override;

  // Type Completion
  bool GetCompleteType(lldb::opaque_compiler_type_t type) override;
  bool IsForcefullyCompleted(lldb::opaque_compiler_type_t type) override;

  // AST related queries
  uint32_t GetPointerByteSize() override;
  CompilerType GetPointerDiffType(bool is_signed) override;
  CompilerType GetSizeType() override;
  unsigned GetPtrAuthKey(lldb::opaque_compiler_type_t type) override;
  unsigned GetPtrAuthDiscriminator(lldb::opaque_compiler_type_t type) override;
  bool GetPtrAuthAddressDiversity(lldb::opaque_compiler_type_t type) override;
  bool HasPointerAuthQualifier(lldb::opaque_compiler_type_t type) override;

  // Accessors
  ConstString GetTypeName(lldb::opaque_compiler_type_t type,
                          bool BaseOnly) override;
  ConstString GetDisplayTypeName(lldb::opaque_compiler_type_t type) override;
  uint32_t GetTypeInfo(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_or_element_compiler_type) override;
  lldb::LanguageType
  GetMinimumLanguage(lldb::opaque_compiler_type_t type) override;
  lldb::TypeClass GetTypeClass(lldb::opaque_compiler_type_t type) override;

  // Creating related types
  CompilerType GetArrayElementType(lldb::opaque_compiler_type_t type,
                                   ExecutionContextScope *exe_scope) override;
  CompilerType GetArrayType(lldb::opaque_compiler_type_t type,
                            uint64_t size) override;
  CompilerType GetCanonicalType(lldb::opaque_compiler_type_t type) override;
  CompilerType
  GetEnumerationIntegerType(lldb::opaque_compiler_type_t type) override;
  void ForEachEnumerator(
      lldb::opaque_compiler_type_t type,
      std::function<bool(const CompilerType &integer_type, ConstString name,
                         const llvm::APSInt &value)> const &callback) override;
  int GetFunctionArgumentCount(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFunctionArgumentTypeAtIndex(lldb::opaque_compiler_type_t type,
                                              size_t idx) override;
  CompilerType
  GetFunctionReturnType(lldb::opaque_compiler_type_t type) override;
  size_t GetNumMemberFunctions(lldb::opaque_compiler_type_t type) override;
  TypeMemberFunctionImpl
  GetMemberFunctionAtIndex(lldb::opaque_compiler_type_t type,
                           size_t idx) override;
  CompilerType GetPointeeType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetPointerType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetTypeForFormatters(lldb::opaque_compiler_type_t type) override;

  // Exploring the type
  const llvm::fltSemantics &GetFloatTypeSemantics(size_t byte_size,
                                                  lldb::Format format) override;
  llvm::Expected<uint64_t>
  GetBitSize(lldb::opaque_compiler_type_t type,
             ExecutionContextScope *exe_scope) override;
  lldb::Encoding GetEncoding(lldb::opaque_compiler_type_t type) override;
  lldb::Format GetFormat(lldb::opaque_compiler_type_t type) override;
  llvm::Expected<uint32_t>
  GetNumChildren(lldb::opaque_compiler_type_t type,
                 bool omit_empty_base_classes,
                 const ExecutionContext *exe_ctx) override;
  lldb::BasicType
  GetBasicTypeEnumeration(lldb::opaque_compiler_type_t type) override;
  bool IsPromotableIntegerType(lldb::opaque_compiler_type_t type) override;
  CompilerType
  GetPromotedIntegerType(lldb::opaque_compiler_type_t type) override;
  uint32_t GetNumFields(lldb::opaque_compiler_type_t type) override;
  CompilerType GetFieldAtIndex(lldb::opaque_compiler_type_t type, size_t idx,
                               std::string &name, uint64_t *bit_offset_ptr,
                               uint32_t *bitfield_bit_size_ptr,
                               bool *is_bitfield_ptr) override;
  uint32_t GetNumDirectBaseClasses(lldb::opaque_compiler_type_t type) override;
  uint32_t GetNumVirtualBaseClasses(lldb::opaque_compiler_type_t type) override;
  CompilerType GetDirectBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                         size_t idx,
                                         uint32_t *bit_offset_ptr) override;
  CompilerType GetVirtualBaseClassAtIndex(lldb::opaque_compiler_type_t type,
                                          size_t idx,
                                          uint32_t *bit_offset_ptr) override;
  llvm::Expected<CompilerType>
  GetDereferencedType(lldb::opaque_compiler_type_t type,
                      ExecutionContext *exe_ctx, std::string &deref_name,
                      uint32_t &deref_byte_size, int32_t &deref_byte_offset,
                      ValueObject *valobj, uint64_t &language_flags) override;
  llvm::Expected<CompilerType> GetChildCompilerTypeAtIndex(
      lldb::opaque_compiler_type_t type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags) override;
  llvm::Expected<uint32_t>
  GetIndexOfChildWithName(lldb::opaque_compiler_type_t type,
                          llvm::StringRef name,
                          bool omit_empty_base_classes) override;
  size_t
  GetIndexOfChildMemberWithName(lldb::opaque_compiler_type_t type,
                                llvm::StringRef name,
                                bool omit_empty_base_classes,
                                std::vector<uint32_t> &child_indexes) override;

  // Dumping types
#ifndef NDEBUG
  LLVM_DUMP_METHOD void dump(lldb::opaque_compiler_type_t type) const override;
#endif

  bool DumpTypeValue(lldb::opaque_compiler_type_t type, Stream &s,
                     lldb::Format format, const DataExtractor &data,
                     lldb::offset_t data_offset, size_t data_byte_size,
                     uint32_t bitfield_bit_size, uint32_t bitfield_bit_offset,
                     ExecutionContextScope *exe_scope) override;
  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type,
      lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) override;
  void DumpTypeDescription(
      lldb::opaque_compiler_type_t type, Stream &s,
      lldb::DescriptionLevel level = lldb::eDescriptionLevelFull) override;
  void Dump(llvm::raw_ostream &output, llvm::StringRef filter,
            bool show_color) override;

  bool IsRuntimeGeneratedType(lldb::opaque_compiler_type_t type) override;

  bool IsPointerOrReferenceType(lldb::opaque_compiler_type_t type,
                                CompilerType *pointee_type) override;
  unsigned GetTypeQualifiers(lldb::opaque_compiler_type_t type) override;
  std::optional<size_t>
  GetTypeBitAlign(lldb::opaque_compiler_type_t type,
                  ExecutionContextScope *exe_scope) override;
  CompilerType GetBasicTypeFromAST(lldb::BasicType basic_type) override;
  CompilerType CreateGenericFunctionPrototype() override;
  CompilerType GetBuiltinTypeByName(ConstString name) override;
  CompilerType GetBuiltinTypeForEncodingAndBitSize(lldb::Encoding encoding,
                                                   size_t bit_size) override;
  bool IsBeingDefined(lldb::opaque_compiler_type_t type) override;
  bool IsConst(lldb::opaque_compiler_type_t type) override;
  uint32_t IsHomogeneousAggregate(lldb::opaque_compiler_type_t type,
                                  CompilerType *base_type_ptr) override;
  bool IsPolymorphicClass(lldb::opaque_compiler_type_t type) override;
  bool IsTypedefType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetTypedefedType(lldb::opaque_compiler_type_t type) override;
  bool IsVectorType(lldb::opaque_compiler_type_t type,
                    CompilerType *element_type, uint64_t *size) override;
  CompilerType
  GetFullyUnqualifiedType(lldb::opaque_compiler_type_t type) override;
  CompilerType GetNonReferenceType(lldb::opaque_compiler_type_t type) override;
  bool IsReferenceType(lldb::opaque_compiler_type_t type,
                       CompilerType *pointee_type, bool *is_rvalue) override;

  CompilerType GetDirectNestedTypeWithName(lldb::opaque_compiler_type_t type,
                                           llvm::StringRef name) override;

private:
  friend class clike::Builder;

  /// Acquire the locks (see GetLockOrder) without an associated Type* yet, for
  /// methods that allocate/complete a *new* node rather than starting from an
  /// existing opaque_compiler_type_t (e.g. GetPointerDiffType,
  /// GetBasicTypeFromAST, CreateGenericFunctionPrototype).
  [[nodiscard]] LockSet LockForWrite() {
    return LockSet(GetLockOrder(), /*exclusive=*/true);
  }
  [[nodiscard]] LockSet LockForRead() const {
    return LockSet(GetLockOrder(), /*exclusive=*/false);
  }

  /// Completion, assuming the write lock is already held by the caller (see
  /// the locking note above GetTypeForWrite/GetTypeForRead). The public
  /// GetCompleteType is a thin locking wrapper around this; so are the other
  /// TypeSystemClike.cpp methods that lazily complete a type before reading
  /// its fields.
  clike::Type *CompleteTypeAssumingWriteLocked(clike::Type *type);

  /// Completes \p type and every record it contains by value (as a field or
  /// array element), as the alignment of a record depends on the
  /// alignment of these members. Assumes the write lock is held.
  void CompleteForAlignmentAssumingWriteLocked(clike::Type *type);

  /// GetBasicTypeFromAST, assuming the write lock is already held. Used by
  /// query methods that already hold the lock themselves (e.g.
  /// GetPromotedIntegerType).
  CompilerType
  GetBasicTypeFromASTAssumingWriteLocked(lldb::BasicType basic_type);

  /// GetTypeName, assuming the write lock is already held. Used by
  /// DumpTypeDescription, which needs a type's name for several of its own
  /// Type* nodes while already holding the lock for the whole dump.
  ConstString GetTypeNameAssumingWriteLocked(clike::Type *t, bool BaseOnly);

  /// GetDisplayTypeName, assuming the write lock is already held. Used by
  /// GetChildCompilerTypeAtIndexImpl to name an incomplete pointee in an
  /// error message while already holding the lock.
  ConstString GetDisplayTypeNameAssumingWriteLocked(clike::Type *t);

  /// Helper for DumpTypeDescription; assumes the write lock is already held.
  void AppendMemberDeclAssumingWriteLocked(Stream &s, clike::Type *field_type,
                                           llvm::StringRef name);

  bool IsArrayTypeImpl(const clike::Type *type, CompilerType *element_type,
                       uint64_t *size, bool *is_incomplete);

  bool IsIntegerTypeImpl(const clike::Type *type, bool &is_signed);

  bool IsPromotableIntegerTypeImpl(const clike::Type *type);

  llvm::Expected<uint32_t> GetNumChildrenImpl(clike::Type *type,
                                              bool omit_empty_base_classes,
                                              const ExecutionContext *exe_ctx);

  llvm::Expected<CompilerType> GetChildCompilerTypeAtIndexImpl(
      clike::Type *type, ExecutionContext *exe_ctx, size_t idx,
      bool transparent_pointers, bool omit_empty_base_classes,
      bool ignore_array_bounds, std::string &child_name,
      uint32_t &child_byte_size, int32_t &child_byte_offset,
      uint32_t &child_bitfield_bit_size, uint32_t &child_bitfield_bit_offset,
      bool &child_is_base_class, bool &child_is_deref_of_parent,
      ValueObject *valobj, uint64_t &language_flags);

  llvm::Expected<uint32_t>
  GetIndexOfChildWithNameImpl(clike::Type *type, llvm::StringRef name,
                              bool omit_empty_base_classes);

  // Recursive worker for GetIndexOfChildMemberWithName. An unnamed (anonymous
  // union/struct) field is transparently searched, as it injects its members
  // into its immediately enclosing record.
  size_t
  GetIndexOfChildMemberWithNameImpl(clike::Type *type, llvm::StringRef name,
                                    bool omit_empty_base_classes,
                                    std::vector<uint32_t> &child_indexes);

  /// Recursive worker for GetFullyUnqualifiedType: strip top-level
  /// cv-qualifiers and recurse through pointer/array pointees so the whole
  /// type is cv-unqualified (see the definition for details).
  clike::Type *GetFullyUnqualifiedTypeImpl(clike::Type *type);

  std::string m_display_name;
  llvm::Triple m_triple;
  clike::Context m_context;

  /// Protects m_context (and thus every Type it owns). See the locking note
  /// above GetTypeForWrite/GetTypeForRead.
  mutable llvm::sys::RWMutex m_mutex;

protected:
  /// Populate \p out with the instances a query rooted here may reach, in any
  /// order and possibly with duplicates -- GetLockOrder sorts and dedups.
  /// Must not take any TypeSystemClike lock: it runs before they are acquired.
  /// Called with m_lock_order_mutex held.
  ///
  /// The base implementation contributes this instance plus, when it has a
  /// symbol file, the type systems of the Clang modules that symbol file
  /// imports (transitively). That list comes straight out of the DWARF -- see
  /// SymbolFile::ForEachExternalModule -- so it is known without parsing a
  /// single type, which is what lets the set be complete before locking.
  virtual void
  AppendLockOrder(llvm::SmallVectorImpl<TypeSystemClike *> &out) const;

  /// Whether AppendLockOrder can return a different set over time, in which
  /// case GetLockOrder must recompute on every call instead of caching. Always
  /// true today: the gmodules import graph is fixed once the symbol file is
  /// known, but every set also folds in the scratch instances' sets, and those
  /// follow their target's image list.
  virtual bool HasDynamicLockOrder() const { return true; }

  /// Append every TypeSystemClike \p module already has to \p out. Shared by
  /// the base AppendLockOrder (for imported Clang modules) and the scratch
  /// override (for the target's images).
  static void
  AppendClikeTypeSystemsOf(Module &module,
                           llvm::SmallVectorImpl<TypeSystemClike *> &out);

  /// Like AppendClikeTypeSystemsOf, but *creates* \p module's TypeSystemClike
  /// if it has none yet. Used for the Clang modules our debug info imports:
  /// their type systems are created lazily by the very query that first reaches
  /// into them (see DWARFASTParserClike::FindClangModuleDefinitionType), so
  /// waiting for one to exist means it is missing from the set exactly when it
  /// is first needed. The DWARF bounds this list to the real import graph, so
  /// it is not the open-ended cost that doing the same for every image would
  /// be.
  static void AppendOrCreateClikeTypeSystemOf(
      Module &module, llvm::SmallVectorImpl<TypeSystemClike *> &out);

  /// Every live scratch instance, so a module instance can find the scratches
  /// that may reach it. A module cannot work that out from its own state -- a
  /// Module belongs to any number of Targets and nothing maps back the other
  /// way -- and it has to be knowable *before* locking, so the scratches
  /// register here as they are created rather than being discovered from a
  /// query's execution context.
  static void RegisterScratchInstance(TypeSystemClike *scratch);
  static void UnregisterScratchInstance(TypeSystemClike *scratch);
  static llvm::SmallVector<TypeSystemClike *, 2> GetScratchInstances();

private:
  /// GetLockOrder's cache, used only when !HasDynamicLockOrder().
  mutable llvm::SmallVector<TypeSystemClike *, 4> m_lock_order;
  mutable bool m_lock_order_valid = false;

  /// Guards m_lock_order only. Separate from m_mutex, and never held while any
  /// m_mutex is: the lock order has to be readable before the locks it
  /// describes are taken.
  mutable std::mutex m_lock_order_mutex;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_PLUGINS_TYPESYSTEM_CLIKE_TYPESYSTEMCLIKE_H
