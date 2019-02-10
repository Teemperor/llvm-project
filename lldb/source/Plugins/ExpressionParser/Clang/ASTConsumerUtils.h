//===-- ClangExpressionParser.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_ASTConsumerUtils_h_
#define liblldb_ASTConsumerUtils_h_

#include "clang/Sema/MultiplexExternalSemaSource.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaConsumer.h"

namespace lldb_private {

/// \brief wraps an ExternalASTSource in an ExternalSemaSource. No functional
/// difference between the original source and this wrapper intended.
class ExternalASTSourceWrapper : public clang::ExternalSemaSource {
  ExternalASTSource *m_Source;

public:
  ExternalASTSourceWrapper(ExternalASTSource *Source) : m_Source(Source) {
    assert(m_Source && "Can't wrap nullptr ExternalASTSource");
  }

  virtual clang::Decl *GetExternalDecl(uint32_t ID) override {
    return m_Source->GetExternalDecl(ID);
  }

  virtual clang::Selector GetExternalSelector(uint32_t ID) override {
    return m_Source->GetExternalSelector(ID);
  }

  virtual uint32_t GetNumExternalSelectors() override {
    return m_Source->GetNumExternalSelectors();
  }

  virtual clang::Stmt *GetExternalDeclStmt(uint64_t Offset) override {
    return m_Source->GetExternalDeclStmt(Offset);
  }

  virtual clang::CXXCtorInitializer **
  GetExternalCXXCtorInitializers(uint64_t Offset) override {
    return m_Source->GetExternalCXXCtorInitializers(Offset);
  }

  virtual clang::CXXBaseSpecifier *
  GetExternalCXXBaseSpecifiers(uint64_t Offset) override {
    return m_Source->GetExternalCXXBaseSpecifiers(Offset);
  }

  virtual void updateOutOfDateIdentifier(clang::IdentifierInfo &II) override {
    m_Source->updateOutOfDateIdentifier(II);
  }

  virtual bool FindExternalVisibleDeclsByName(const clang::DeclContext *DC,
                                              clang::DeclarationName Name) override {
    return m_Source->FindExternalVisibleDeclsByName(DC, Name);
  }

  virtual void completeVisibleDeclsMap(const clang::DeclContext *DC) override {
    m_Source->completeVisibleDeclsMap(DC);
  }

  virtual clang::Module *getModule(unsigned ID) override {
    return m_Source->getModule(ID);
  }

  virtual llvm::Optional<ASTSourceDescriptor>
  getSourceDescriptor(unsigned ID) override {
    return m_Source->getSourceDescriptor(ID);
  }

  virtual ExtKind hasExternalDefinitions(const clang::Decl *D) override {
    return m_Source->hasExternalDefinitions(D);
  }

  virtual void
  FindExternalLexicalDecls(const clang::DeclContext *DC,
                           llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
                           llvm::SmallVectorImpl<clang::Decl *> &Result) override {
    m_Source->FindExternalLexicalDecls(DC, IsKindWeWant, Result);
  }

  virtual void FindFileRegionDecls(clang::FileID File, unsigned Offset,
                                   unsigned Length,
                                   llvm::SmallVectorImpl<clang::Decl *> &Decls) override {
    m_Source->FindFileRegionDecls(File, Offset, Length, Decls);
  }

  virtual void CompleteRedeclChain(const clang::Decl *D) override {
    m_Source->CompleteRedeclChain(D);
  }

  virtual void CompleteType(clang::TagDecl *Tag) override {
    m_Source->CompleteType(Tag);
  }

  virtual void CompleteType(clang::ObjCInterfaceDecl *Class) override {
    m_Source->CompleteType(Class);
  }

  virtual void ReadComments() override { m_Source->ReadComments(); }

  virtual void StartedDeserializing() override {
    m_Source->StartedDeserializing();
  }

  virtual void FinishedDeserializing() override {
    m_Source->FinishedDeserializing();
  }

  virtual void StartTranslationUnit(clang::ASTConsumer *Consumer) override {
    m_Source->StartTranslationUnit(Consumer);
  }

  virtual void PrintStats() override { m_Source->PrintStats(); }

  virtual bool layoutRecordType(
      const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &BaseOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &VirtualBaseOffsets)
      override {
    return m_Source->layoutRecordType(Record, Size, Alignment, FieldOffsets,
                                      BaseOffsets, VirtualBaseOffsets);
  }
};

/// ASTConsumer - This is an abstract interface that should be implemented by
/// clients that read ASTs.  This abstraction layer allows the client to be
/// independent of the AST producer (e.g. parser vs AST dump file reader, etc).
class ASTConsumerForwarder : public clang::SemaConsumer {
  clang::ASTConsumer *m_c;
  clang::SemaConsumer *m_sc;

public:
  ASTConsumerForwarder(clang::ASTConsumer *c) : m_c(c) {
    m_sc = llvm::dyn_cast<clang::SemaConsumer>(m_c);
  }

  void Initialize(clang::ASTContext &Context) override { m_c->Initialize(Context); }

  bool HandleTopLevelDecl(clang::DeclGroupRef D) override {
    return m_c->HandleTopLevelDecl(D);
  }

  void HandleInlineFunctionDefinition(clang::FunctionDecl *D) override {
    m_c->HandleInlineFunctionDefinition(D);
  }

  void HandleInterestingDecl(clang::DeclGroupRef D) override {
    m_c->HandleInterestingDecl(D);
  }

  void HandleTranslationUnit(clang::ASTContext &Ctx) override {
    m_c->HandleTranslationUnit(Ctx);
  }

  void HandleTagDeclDefinition(clang::TagDecl *D) override {
    m_c->HandleTagDeclDefinition(D);
  }

  void HandleTagDeclRequiredDefinition(const clang::TagDecl *D) override {
    m_c->HandleTagDeclRequiredDefinition(D);
  }

  void HandleCXXImplicitFunctionInstantiation(clang::FunctionDecl *D) override {
    m_c->HandleCXXImplicitFunctionInstantiation(D);
  }

  void HandleTopLevelDeclInObjCContainer(clang::DeclGroupRef D) override {
    m_c->HandleTopLevelDeclInObjCContainer(D);
  }

  void HandleImplicitImportDecl(clang::ImportDecl *D) override {
    m_c->HandleImplicitImportDecl(D);
  }

  void CompleteTentativeDefinition(clang::VarDecl *D) override {
    m_c->CompleteTentativeDefinition(D);
  }

  void AssignInheritanceModel(clang::CXXRecordDecl *RD) override {
    m_c->AssignInheritanceModel(RD);
  }

  void HandleCXXStaticMemberVarInstantiation(clang::VarDecl *D) override {
    m_c->HandleCXXStaticMemberVarInstantiation(D);
  }

  void HandleVTable(clang::CXXRecordDecl *RD) override { m_c->HandleVTable(RD); }

  clang::ASTMutationListener *GetASTMutationListener() override {
    return m_c->GetASTMutationListener();
  }

  clang::ASTDeserializationListener *GetASTDeserializationListener() override {
    return m_c->GetASTDeserializationListener();
  }

  void PrintStats() override { m_c->PrintStats(); }

  void InitializeSema(clang::Sema &S) override {
    if (m_sc)
      m_sc->InitializeSema(S);
  }

  /// Inform the semantic consumer that Sema is no longer available.
  void ForgetSema() override {
    if (m_sc)
      m_sc->ForgetSema();
  }

  bool shouldSkipFunctionBody(clang::Decl *D) override {
    return m_c->shouldSkipFunctionBody(D);
  }
};

/// An abstract interface that should be implemented by
/// external AST sources that also provide information for semantic
/// analysis.
class MyMultiplexExternalSemaSource : public clang::ExternalSemaSource {

private:
  llvm::SmallVector<clang::ExternalSemaSource *, 2> Sources; // doesn't own them.

public:
  /// Constructs a new multiplexing external sema source and appends the
  /// given element to it.
  ///
  ///\param[in] s1 - A non-null (old) ExternalSemaSource.
  ///\param[in] s2 - A non-null (new) ExternalSemaSource.
  ///
  MyMultiplexExternalSemaSource(clang::ExternalSemaSource &s1,
                                clang::ExternalSemaSource &s2) {
    Sources.push_back(&s1);
    Sources.push_back(&s2);
  }

  ~MyMultiplexExternalSemaSource() override {}

  /// Appends new source to the source list.
  ///
  ///\param[in] source - An ExternalSemaSource.
  ///
  void addSource(clang::ExternalSemaSource &source) { Sources.push_back(&source); }

  //===--------------------------------------------------------------------===//
  // ExternalASTSource.
  //===--------------------------------------------------------------------===//

  /// Resolve a declaration ID into a declaration, potentially
  /// building a new declaration.
  clang::Decl *GetExternalDecl(uint32_t ID) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (clang::Decl *Result = Sources[i]->GetExternalDecl(ID))
        return Result;
    return nullptr;
  }

  /// Complete the redeclaration chain if it's been extended since the
  /// previous generation of the AST source.
  void CompleteRedeclChain(const clang::Decl *D) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->CompleteRedeclChain(D);
  }

  /// Resolve a selector ID into a selector.
  clang::Selector GetExternalSelector(uint32_t ID) override {
    clang::Selector Sel;
    for (size_t i = 0; i < Sources.size(); ++i) {
      Sel = Sources[i]->GetExternalSelector(ID);
      if (!Sel.isNull())
        return Sel;
    }
    return Sel;
  }

  /// Returns the number of selectors known to the external AST
  /// source.
  uint32_t GetNumExternalSelectors() override {
    uint32_t total = 0;
    for (size_t i = 0; i < Sources.size(); ++i)
      total += Sources[i]->GetNumExternalSelectors();
    return total;
  }

  /// Resolve the offset of a statement in the decl stream into
  /// a statement.
  clang::Stmt *GetExternalDeclStmt(uint64_t Offset) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (clang::Stmt *Result = Sources[i]->GetExternalDeclStmt(Offset))
        return Result;
    return nullptr;
  }

  /// Resolve the offset of a set of C++ base specifiers in the decl
  /// stream into an array of specifiers.
  clang::CXXBaseSpecifier *GetExternalCXXBaseSpecifiers(uint64_t Offset) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (clang::CXXBaseSpecifier *R =
              Sources[i]->GetExternalCXXBaseSpecifiers(Offset))
        return R;
    return nullptr;
  }

  /// Resolve a handle to a list of ctor initializers into the list of
  /// initializers themselves.
  clang::CXXCtorInitializer **
  GetExternalCXXCtorInitializers(uint64_t Offset) override {
    for (auto *S : Sources)
      if (auto *R = S->GetExternalCXXCtorInitializers(Offset))
        return R;
    return nullptr;
  }

  ExtKind hasExternalDefinitions(const clang::Decl *D) override {
    for (const auto &S : Sources)
      if (auto EK = S->hasExternalDefinitions(D))
        if (EK != EK_ReplyHazy)
          return EK;
    return EK_ReplyHazy;
  }

  /// Find all declarations with the given name in the
  /// given context.
  bool FindExternalVisibleDeclsByName(const clang::DeclContext *DC,
                                      clang::DeclarationName Name) override {
    // bool AnyDeclsFound = false;
    for (size_t i = 0; i < Sources.size(); ++i)
      if (Sources[i]->FindExternalVisibleDeclsByName(DC, Name))
        return true;
    return false;
  }

  /// Ensures that the table of all visible declarations inside this
  /// context is up to date.
  void completeVisibleDeclsMap(const clang::DeclContext *DC) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->completeVisibleDeclsMap(DC);
  }

  /// Finds all declarations lexically contained within the given
  /// DeclContext, after applying an optional filter predicate.
  ///
  /// \param IsKindWeWant a predicate function that returns true if the passed
  /// declaration kind is one we are looking for.
  void
  FindExternalLexicalDecls(const clang::DeclContext *DC,
                           llvm::function_ref<bool(clang::Decl::Kind)> IsKindWeWant,
                           llvm::SmallVectorImpl<clang::Decl *> &Result) override {
    for (size_t i = 0; i < Sources.size(); ++i) {
      Sources[i]->FindExternalLexicalDecls(DC, IsKindWeWant, Result);
      if (!Result.empty())
        return;
    }
  }

  /// Get the decls that are contained in a file in the Offset/Length
  /// range. \p Length can be 0 to indicate a point at \p Offset instead of
  /// a range.
  void FindFileRegionDecls(clang::FileID File, unsigned Offset, unsigned Length,
                           llvm::SmallVectorImpl<clang::Decl *> &Decls) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->FindFileRegionDecls(File, Offset, Length, Decls);
  }

  /// Gives the external AST source an opportunity to complete
  /// an incomplete type.
  void CompleteType(clang::TagDecl *Tag) override {
    while (!Tag->isCompleteDefinition())
      for (size_t i = 0; i < Sources.size(); ++i) {
        Sources[i]->CompleteType(Tag);
        if (Tag->isCompleteDefinition())
          break;
      }
  }

  /// Gives the external AST source an opportunity to complete an
  /// incomplete Objective-C class.
  ///
  /// This routine will only be invoked if the "externally completed" bit is
  /// set on the ObjCInterfaceDecl via the function
  /// \c ObjCInterfaceDecl::setExternallyCompleted().
  void CompleteType(clang::ObjCInterfaceDecl *Class) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->CompleteType(Class);
  }

  /// Loads comment ranges.
  void ReadComments() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadComments();
  }

  /// Notify ExternalASTSource that we started deserialization of
  /// a decl or type so until FinishedDeserializing is called there may be
  /// decls that are initializing. Must be paired with FinishedDeserializing.
  void StartedDeserializing() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->StartedDeserializing();
  }

  /// Notify ExternalASTSource that we finished the deserialization of
  /// a decl or type. Must be paired with StartedDeserializing.
  void FinishedDeserializing() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->FinishedDeserializing();
  }

  /// Function that will be invoked when we begin parsing a new
  /// translation unit involving this external AST source.
  void StartTranslationUnit(clang::ASTConsumer *Consumer) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->StartTranslationUnit(Consumer);
  }

  /// Print any statistics that have been gathered regarding
  /// the external AST source.
  void PrintStats() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->PrintStats();
  }

  /// Retrieve the module that corresponds to the given module ID.
  clang::Module *getModule(unsigned ID) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (auto M = Sources[i]->getModule(ID))
        return M;
    return nullptr;
  }

  bool DeclIsFromPCHWithObjectFile(const clang::Decl *D) override {
    for (auto *S : Sources)
      if (S->DeclIsFromPCHWithObjectFile(D))
        return true;
    return false;
  }

  /// Perform layout on the given record.
  ///
  /// This routine allows the external AST source to provide an specific
  /// layout for a record, overriding the layout that would normally be
  /// constructed. It is intended for clients who receive specific layout
  /// details rather than source code (such as LLDB). The client is expected
  /// to fill in the field offsets, base offsets, virtual base offsets, and
  /// complete object size.
  ///
  /// \param Record The record whose layout is being requested.
  ///
  /// \param Size The final size of the record, in bits.
  ///
  /// \param Alignment The final alignment of the record, in bits.
  ///
  /// \param FieldOffsets The offset of each of the fields within the record,
  /// expressed in bits. All of the fields must be provided with offsets.
  ///
  /// \param BaseOffsets The offset of each of the direct, non-virtual base
  /// classes. If any bases are not given offsets, the bases will be laid
  /// out according to the ABI.
  ///
  /// \param VirtualBaseOffsets The offset of each of the virtual base classes
  /// (either direct or not). If any bases are not given offsets, the bases will
  /// be laid out according to the ABI.
  ///
  /// \returns true if the record layout was provided, false otherwise.
  bool layoutRecordType(
      const clang::RecordDecl *Record, uint64_t &Size, uint64_t &Alignment,
      llvm::DenseMap<const clang::FieldDecl *, uint64_t> &FieldOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &BaseOffsets,
      llvm::DenseMap<const clang::CXXRecordDecl *, clang::CharUnits> &VirtualBaseOffsets)
      override {
    for (size_t i = 0; i < Sources.size(); ++i)
      if (Sources[i]->layoutRecordType(Record, Size, Alignment, FieldOffsets,
                                       BaseOffsets, VirtualBaseOffsets))
        return true;
    return false;
  }

  /// Return the amount of memory used by memory buffers, breaking down
  /// by heap-backed versus mmap'ed memory.
  void getMemoryBufferSizes(MemoryBufferSizes &sizes) const override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->getMemoryBufferSizes(sizes);
  }

  //===--------------------------------------------------------------------===//
  // ExternalSemaSource.
  //===--------------------------------------------------------------------===//

  /// Initialize the semantic source with the Sema instance
  /// being used to perform semantic analysis on the abstract syntax
  /// tree.
  void InitializeSema(clang::Sema &S) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->InitializeSema(S);
  }

  /// Inform the semantic consumer that Sema is no longer available.
  void ForgetSema() override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ForgetSema();
  }

  /// Load the contents of the global method pool for a given
  /// selector.
  void ReadMethodPool(clang::Selector Sel) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadMethodPool(Sel);
  }

  /// Load the contents of the global method pool for a given
  /// selector if necessary.
  void updateOutOfDateSelector(clang::Selector Sel) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->updateOutOfDateSelector(Sel);
  }

  /// Load the set of namespaces that are known to the external source,
  /// which will be used during typo correction.
  void
  ReadKnownNamespaces(llvm::SmallVectorImpl<clang::NamespaceDecl *> &Namespaces) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadKnownNamespaces(Namespaces);
  }

  /// Load the set of used but not defined functions or variables with
  /// internal linkage, or used but not defined inline functions.
  void ReadUndefinedButUsed(
      llvm::MapVector<clang::NamedDecl *, clang::SourceLocation> &Undefined) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadUndefinedButUsed(Undefined);
  }

  void ReadMismatchingDeleteExpressions(
      llvm::MapVector<clang::FieldDecl *,
                      llvm::SmallVector<std::pair<clang::SourceLocation, bool>, 4>>
          &Exprs) override {
    for (auto &Source : Sources)
      Source->ReadMismatchingDeleteExpressions(Exprs);
  }

  /// Do last resort, unqualified lookup on a LookupResult that
  /// Sema cannot find.
  ///
  /// \param R a LookupResult that is being recovered.
  ///
  /// \param S the Scope of the identifier occurrence.
  ///
  /// \return true to tell Sema to recover using the LookupResult.
  bool LookupUnqualified(clang::LookupResult &R, clang::Scope *S) override {
    for (size_t i = 0; i < Sources.size(); ++i) {
      Sources[i]->LookupUnqualified(R, S);
      if (!R.empty())
        break;
    }

    return !R.empty();
  }

  /// Read the set of tentative definitions known to the external Sema
  /// source.
  ///
  /// The external source should append its own tentative definitions to the
  /// given vector of tentative definitions. Note that this routine may be
  /// invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadTentativeDefinitions(llvm::SmallVectorImpl<clang::VarDecl *> &Defs) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadTentativeDefinitions(Defs);
  }

  /// Read the set of unused file-scope declarations known to the
  /// external Sema source.
  ///
  /// The external source should append its own unused, filed-scope to the
  /// given vector of declarations. Note that this routine may be
  /// invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadUnusedFileScopedDecls(
      llvm::SmallVectorImpl<const clang::DeclaratorDecl *> &Decls) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadUnusedFileScopedDecls(Decls);
  }

  /// Read the set of delegating constructors known to the
  /// external Sema source.
  ///
  /// The external source should append its own delegating constructors to the
  /// given vector of declarations. Note that this routine may be
  /// invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadDelegatingConstructors(
      llvm::SmallVectorImpl<clang::CXXConstructorDecl *> &Decls) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadDelegatingConstructors(Decls);
  }

  /// Read the set of ext_vector type declarations known to the
  /// external Sema source.
  ///
  /// The external source should append its own ext_vector type declarations to
  /// the given vector of declarations. Note that this routine may be
  /// invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadExtVectorDecls(llvm::SmallVectorImpl<clang::TypedefNameDecl *> &Decls) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadExtVectorDecls(Decls);
  }

  /// Read the set of potentially unused typedefs known to the source.
  ///
  /// The external source should append its own potentially unused local
  /// typedefs to the given vector of declarations. Note that this routine may
  /// be invoked multiple times; the external source should take care not to
  /// introduce the same declarations repeatedly.
  void ReadUnusedLocalTypedefNameCandidates(
      llvm::SmallSetVector<const clang::TypedefNameDecl *, 4> &Decls) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadUnusedLocalTypedefNameCandidates(Decls);
  }

  /// Read the set of referenced selectors known to the
  /// external Sema source.
  ///
  /// The external source should append its own referenced selectors to the
  /// given vector of selectors. Note that this routine
  /// may be invoked multiple times; the external source should take care not
  /// to introduce the same selectors repeatedly.
  void ReadReferencedSelectors(
      llvm::SmallVectorImpl<std::pair<clang::Selector, clang::SourceLocation>> &Sels) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadReferencedSelectors(Sels);
  }

  /// Read the set of weak, undeclared identifiers known to the
  /// external Sema source.
  ///
  /// The external source should append its own weak, undeclared identifiers to
  /// the given vector. Note that this routine may be invoked multiple times;
  /// the external source should take care not to introduce the same identifiers
  /// repeatedly.
  void ReadWeakUndeclaredIdentifiers(
      llvm::SmallVectorImpl<std::pair<clang::IdentifierInfo *, clang::WeakInfo>> &WI) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadWeakUndeclaredIdentifiers(WI);
  }

  /// Read the set of used vtables known to the external Sema source.
  ///
  /// The external source should append its own used vtables to the given
  /// vector. Note that this routine may be invoked multiple times; the external
  /// source should take care not to introduce the same vtables repeatedly.
  void ReadUsedVTables(llvm::SmallVectorImpl<clang::ExternalVTableUse> &VTables) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadUsedVTables(VTables);
  }

  /// Read the set of pending instantiations known to the external
  /// Sema source.
  ///
  /// The external source should append its own pending instantiations to the
  /// given vector. Note that this routine may be invoked multiple times; the
  /// external source should take care not to introduce the same instantiations
  /// repeatedly.
  void ReadPendingInstantiations(
      llvm::SmallVectorImpl<std::pair<clang::ValueDecl *, clang::SourceLocation>> &Pending)
      override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadPendingInstantiations(Pending);
  }

  /// Read the set of late parsed template functions for this source.
  ///
  /// The external source should insert its own late parsed template functions
  /// into the map. Note that this routine may be invoked multiple times; the
  /// external source should take care not to introduce the same map entries
  /// repeatedly.
  void ReadLateParsedTemplates(
      llvm::MapVector<const clang::FunctionDecl *, std::unique_ptr<clang::LateParsedTemplate>>
          &LPTMap) override {
    for (size_t i = 0; i < Sources.size(); ++i)
      Sources[i]->ReadLateParsedTemplates(LPTMap);
  }

  /// \copydoc ExternalSemaSource::CorrectTypo
  /// \note Returns the first nonempty correction.
  clang::TypoCorrection CorrectTypo(const clang::DeclarationNameInfo &Typo, int LookupKind,
                             clang::Scope *S, clang::CXXScopeSpec *SS,
                             clang::CorrectionCandidateCallback &CCC,
                             clang::DeclContext *MemberContext, bool EnteringContext,
                             const clang::ObjCObjectPointerType *OPT) override {
    for (size_t I = 0, E = Sources.size(); I < E; ++I) {
      if (clang::TypoCorrection C =
              Sources[I]->CorrectTypo(Typo, LookupKind, S, SS, CCC,
                                      MemberContext, EnteringContext, OPT))
        return C;
    }
    return clang::TypoCorrection();
  }

  /// Produces a diagnostic note if one of the attached sources
  /// contains a complete definition for \p T. Queries the sources in list
  /// order until the first one claims that a diagnostic was produced.
  ///
  /// \param Loc the location at which a complete type was required but not
  /// provided
  ///
  /// \param T the \c QualType that should have been complete at \p Loc
  ///
  /// \return true if a diagnostic was produced, false otherwise.
  bool MaybeDiagnoseMissingCompleteType(clang::SourceLocation Loc,
                                        clang::QualType T) override {
    for (size_t I = 0, E = Sources.size(); I < E; ++I) {
      if (Sources[I]->MaybeDiagnoseMissingCompleteType(Loc, T))
        return true;
    }
    return false;
  }
};

}
#endif // liblldb_ASTConsumerUtils_h_
