//=== unittests/Sema/ExternalSemaSourceTest.cpp - ExternalSemaSource tests ===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Parse/ParseAST.h"
#include "clang/Sema/ExternalSemaSource.h"
#include "clang/Sema/Sema.h"
#include "clang/Sema/SemaDiagnostic.h"
#include "clang/Sema/TypoCorrection.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"

using namespace clang;
using namespace clang::tooling;

namespace {

// \brief Counts the number of times MaybeDiagnoseMissingCompleteType
// is called. Returns the result it was provided on creation.
class CompleteTypeDiagnoser : public clang::ExternalSemaSource {
public:
  CompleteTypeDiagnoser(bool MockResult) : CallCount(0), Result(MockResult) {}

  bool MaybeDiagnoseMissingCompleteType(SourceLocation L, QualType T) override {
    ++CallCount;
    return Result;
  }

  int CallCount;
  bool Result;
};

/// Counts the number of typo-correcting diagnostics correcting from one name to
/// another while still passing all diagnostics along a chain of consumers.
class DiagnosticWatcher : public clang::DiagnosticConsumer {
  DiagnosticConsumer *Chained;
  std::string FromName;
  std::string ToName;

public:
  DiagnosticWatcher(StringRef From, StringRef To)
      : Chained(nullptr), FromName(From), ToName("'"), SeenCount(0) {
    ToName.append(std::string(To));
    ToName.append("'");
  }

  void HandleDiagnostic(DiagnosticsEngine::Level DiagLevel,
                        const Diagnostic &Info) override {
    if (Chained)
      Chained->HandleDiagnostic(DiagLevel, Info);
    if (Info.getID() - 1 == diag::err_using_directive_member_suggest) {
      const IdentifierInfo *Ident = Info.getArgIdentifier(0);
      const std::string &CorrectedQuotedStr = Info.getArgStdStr(1);
      if (Ident->getName() == FromName && CorrectedQuotedStr == ToName)
        ++SeenCount;
    } else if (Info.getID() == diag::err_no_member_suggest) {
      auto Ident = DeclarationName::getFromOpaqueInteger(Info.getRawArg(0));
      const std::string &CorrectedQuotedStr = Info.getArgStdStr(3);
      if (Ident.getAsString() == FromName && CorrectedQuotedStr == ToName)
        ++SeenCount;
    }
  }

  void clear() override {
    DiagnosticConsumer::clear();
    if (Chained)
      Chained->clear();
  }

  bool IncludeInDiagnosticCounts() const override {
    if (Chained)
      return Chained->IncludeInDiagnosticCounts();
    return false;
  }

  DiagnosticWatcher *Chain(DiagnosticConsumer *ToChain) {
    Chained = ToChain;
    return this;
  }

  int SeenCount;
};

// \brief Always corrects a typo matching CorrectFrom with a new namespace
// with the name CorrectTo.
class NamespaceTypoProvider : public clang::ExternalSemaSource {
  std::string CorrectFrom;
  std::string CorrectTo;
  Sema *CurrentSema;

public:
  NamespaceTypoProvider(StringRef From, StringRef To)
      : CorrectFrom(From), CorrectTo(To), CurrentSema(nullptr), CallCount(0) {}

  void InitializeSema(Sema &S) override { CurrentSema = &S; }

  void ForgetSema() override { CurrentSema = nullptr; }

  TypoCorrection CorrectTypo(const DeclarationNameInfo &Typo, int LookupKind,
                             Scope *S, CXXScopeSpec *SS,
                             CorrectionCandidateCallback &CCC,
                             DeclContext *MemberContext, bool EnteringContext,
                             const ObjCObjectPointerType *OPT) override {
    ++CallCount;
    if (CurrentSema && Typo.getName().getAsString() == CorrectFrom) {
      DeclContext *DestContext = nullptr;
      ASTContext &Context = CurrentSema->getASTContext();
      if (SS)
        DestContext = CurrentSema->computeDeclContext(*SS, EnteringContext);
      if (!DestContext)
        DestContext = Context.getTranslationUnitDecl();
      IdentifierInfo *ToIdent =
          CurrentSema->getPreprocessor().getIdentifierInfo(CorrectTo);
      NamespaceDecl *NewNamespace =
          NamespaceDecl::Create(Context, DestContext, false, Typo.getBeginLoc(),
                                Typo.getLoc(), ToIdent, nullptr, false);
      DestContext->addDecl(NewNamespace);
      TypoCorrection Correction(ToIdent);
      Correction.addCorrectionDecl(NewNamespace);
      return Correction;
    }
    return TypoCorrection();
  }

  int CallCount;
};

class FunctionTypoProvider : public clang::ExternalSemaSource {
  std::string CorrectFrom;
  std::string CorrectTo;
  Sema *CurrentSema;

public:
  FunctionTypoProvider(StringRef From, StringRef To)
      : CorrectFrom(From), CorrectTo(To), CurrentSema(nullptr), CallCount(0) {}

  void InitializeSema(Sema &S) override { CurrentSema = &S; }

  void ForgetSema() override { CurrentSema = nullptr; }

  TypoCorrection CorrectTypo(const DeclarationNameInfo &Typo, int LookupKind,
                             Scope *S, CXXScopeSpec *SS,
                             CorrectionCandidateCallback &CCC,
                             DeclContext *MemberContext, bool EnteringContext,
                             const ObjCObjectPointerType *OPT) override {
    ++CallCount;
    if (CurrentSema && Typo.getName().getAsString() == CorrectFrom) {
      DeclContext *DestContext = nullptr;
      ASTContext &Context = CurrentSema->getASTContext();
      if (SS)
        DestContext = CurrentSema->computeDeclContext(*SS, EnteringContext);
      if (!DestContext)
        DestContext = Context.getTranslationUnitDecl();
      IdentifierInfo *ToIdent =
          CurrentSema->getPreprocessor().getIdentifierInfo(CorrectTo);
      auto *NewFunction = FunctionDecl::Create(
          Context, DestContext, SourceLocation(), SourceLocation(), ToIdent,
          Context.getFunctionType(Context.VoidTy, {}, {}), nullptr, SC_Static,
          /*UsesFPIntrin*/ false);
      DestContext->addDecl(NewFunction);
      TypoCorrection Correction(ToIdent);
      Correction.addCorrectionDecl(NewFunction);
      return Correction;
    }
    return TypoCorrection();
  }

  int CallCount;
};

// Records the DeclContexts passed to StartedQualifiedLookup and
// FinishedQualifiedLookup.
class QualifiedLookupRecorder : public clang::ExternalSemaSource {
public:
  static std::string getName(const DeclContext *DC) {
    if (const auto *ND = dyn_cast<NamedDecl>(DC))
      return ND->getNameAsString();
    return "<TU>";
  }

  void StartedQualifiedLookup(const DeclContext *DC) override {
    Events.push_back("start " + getName(DC));
    ActiveLookups.push_back(DC);
  }

  void FinishedQualifiedLookup(const DeclContext *DC) override {
    Events.push_back("finish " + getName(DC));
    // Every finish must match the innermost started lookup.
    if (ActiveLookups.empty() || ActiveLookups.back() != DC)
      Unbalanced = true;
    else
      ActiveLookups.pop_back();
  }

  std::vector<std::string> Events;
  std::vector<const DeclContext *> ActiveLookups;
  bool Unbalanced = false;
};

// \brief Chains together a vector of DiagnosticWatchers and
// adds a vector of ExternalSemaSources to the CompilerInstance before
// performing semantic analysis.
class ExternalSemaSourceInstaller : public clang::ASTFrontendAction {
  std::vector<DiagnosticWatcher *> Watchers;
  std::vector<llvm::IntrusiveRefCntPtr<clang::ExternalSemaSource>> Sources;
  std::unique_ptr<DiagnosticConsumer> OwnedClient;

protected:
  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &Compiler,
                    llvm::StringRef /* dummy */) override {
    return std::make_unique<clang::ASTConsumer>();
  }

  void ExecuteAction() override {
    CompilerInstance &CI = getCompilerInstance();
    ASSERT_FALSE(CI.hasSema());
    CI.createSema(getTranslationUnitKind(), nullptr);
    ASSERT_TRUE(CI.hasDiagnostics());
    DiagnosticsEngine &Diagnostics = CI.getDiagnostics();
    DiagnosticConsumer *Client = Diagnostics.getClient();
    if (Diagnostics.ownsClient())
      OwnedClient = Diagnostics.takeClient();
    for (size_t I = 0, E = Watchers.size(); I < E; ++I)
      Client = Watchers[I]->Chain(Client);
    Diagnostics.setClient(Client, false);
    for (size_t I = 0, E = Sources.size(); I < E; ++I) {
      Sources[I]->InitializeSema(CI.getSema());
      CI.getSema().addExternalSource(Sources[I]);
    }
    ParseAST(CI.getSema(), CI.getFrontendOpts().ShowStats,
             CI.getFrontendOpts().SkipFunctionBodies);
  }

public:
  void PushSource(llvm::IntrusiveRefCntPtr<clang::ExternalSemaSource> Source) {
    Sources.push_back(std::move(Source));
  }

  void PushWatcher(DiagnosticWatcher *Watcher) { Watchers.push_back(Watcher); }
};

using llvm::makeIntrusiveRefCnt;

// Make sure that the DiagnosticWatcher is not miscounting.
TEST(ExternalSemaSource, DiagCheck) {
  auto Installer = std::make_unique<ExternalSemaSourceInstaller>();
  DiagnosticWatcher Watcher("AAB", "BBB");
  Installer->PushWatcher(&Watcher);
  std::vector<std::string> Args(1, "-std=c++11");
  ASSERT_TRUE(clang::tooling::runToolOnCodeWithArgs(
      std::move(Installer), "namespace AAA { } using namespace AAB;", Args));
  ASSERT_EQ(0, Watcher.SeenCount);
}

// Check that when we add a NamespaceTypeProvider, we use that suggestion
// instead of the usual suggestion we would use above.
TEST(ExternalSemaSource, ExternalTypoCorrectionPrioritized) {
  auto Installer = std::make_unique<ExternalSemaSourceInstaller>();
  auto Provider = makeIntrusiveRefCnt<NamespaceTypoProvider>("AAB", "BBB");
  DiagnosticWatcher Watcher("AAB", "BBB");
  Installer->PushSource(Provider);
  Installer->PushWatcher(&Watcher);
  std::vector<std::string> Args(1, "-std=c++11");
  ASSERT_TRUE(clang::tooling::runToolOnCodeWithArgs(
      std::move(Installer), "namespace AAA { } using namespace AAB;", Args));
  ASSERT_LE(0, Provider->CallCount);
  ASSERT_EQ(1, Watcher.SeenCount);
}

// Check that we use the first successful TypoCorrection returned from an
// ExternalSemaSource.
TEST(ExternalSemaSource, ExternalTypoCorrectionOrdering) {
  auto Installer = std::make_unique<ExternalSemaSourceInstaller>();
  auto First = makeIntrusiveRefCnt<NamespaceTypoProvider>("XXX", "BBB");
  auto Second = makeIntrusiveRefCnt<NamespaceTypoProvider>("AAB", "CCC");
  auto Third = makeIntrusiveRefCnt<NamespaceTypoProvider>("AAB", "DDD");
  DiagnosticWatcher Watcher("AAB", "CCC");
  Installer->PushSource(First);
  Installer->PushSource(Second);
  Installer->PushSource(Third);
  Installer->PushWatcher(&Watcher);
  std::vector<std::string> Args(1, "-std=c++11");
  ASSERT_TRUE(clang::tooling::runToolOnCodeWithArgs(
      std::move(Installer), "namespace AAA { } using namespace AAB;", Args));
  ASSERT_LE(1, First->CallCount);
  ASSERT_LE(1, Second->CallCount);
  ASSERT_EQ(0, Third->CallCount);
  ASSERT_EQ(1, Watcher.SeenCount);
}

// We should only try MaybeDiagnoseMissingCompleteType if we can't otherwise
// solve the problem.
TEST(ExternalSemaSource, TryOtherTacticsBeforeDiagnosing) {
  auto Installer = std::make_unique<ExternalSemaSourceInstaller>();
  auto Diagnoser = makeIntrusiveRefCnt<CompleteTypeDiagnoser>(false);
  Installer->PushSource(Diagnoser);
  std::vector<std::string> Args(1, "-std=c++11");
  // This code hits the class template specialization/class member of a class
  // template specialization checks in Sema::RequireCompleteTypeImpl.
  ASSERT_TRUE(clang::tooling::runToolOnCodeWithArgs(
      std::move(Installer),
      "template <typename T> struct S { class C { }; }; S<char>::C SCInst;",
      Args));
  ASSERT_EQ(0, Diagnoser->CallCount);
}

// The first ExternalSemaSource where MaybeDiagnoseMissingCompleteType returns
// true should be the last one called.
TEST(ExternalSemaSource, FirstDiagnoserTaken) {
  auto Installer = std::make_unique<ExternalSemaSourceInstaller>();
  auto First = makeIntrusiveRefCnt<CompleteTypeDiagnoser>(false);
  auto Second = makeIntrusiveRefCnt<CompleteTypeDiagnoser>(true);
  auto Third = makeIntrusiveRefCnt<CompleteTypeDiagnoser>(true);
  Installer->PushSource(First);
  Installer->PushSource(Second);
  Installer->PushSource(Third);
  std::vector<std::string> Args(1, "-std=c++11");
  ASSERT_FALSE(clang::tooling::runToolOnCodeWithArgs(
      std::move(Installer), "class Incomplete; Incomplete IncompleteInstance;",
      Args));
  ASSERT_EQ(1, First->CallCount);
  ASSERT_EQ(1, Second->CallCount);
  ASSERT_EQ(0, Third->CallCount);
}

// Qualified lookups should be reported to all external sources and be
// balanced.
TEST(ExternalSemaSource, QualifiedLookupNotifications) {
  auto Installer = std::make_unique<ExternalSemaSourceInstaller>();
  auto First = makeIntrusiveRefCnt<QualifiedLookupRecorder>();
  auto Second = makeIntrusiveRefCnt<QualifiedLookupRecorder>();
  Installer->PushSource(First);
  Installer->PushSource(Second);
  std::vector<std::string> Args(1, "-std=c++11");
  ASSERT_TRUE(clang::tooling::runToolOnCodeWithArgs(
      std::move(Installer),
      "namespace A { int x; } int y = A::x; int z = ::A::x;", Args));
  for (QualifiedLookupRecorder *R : {First.get(), Second.get()}) {
    EXPECT_FALSE(R->Unbalanced);
    EXPECT_TRUE(R->ActiveLookups.empty());
    // 'A::x' looks up 'x' in A, '::A::x' additionally looks up 'A' in the TU.
    // Sema may repeat the same lookup, so only check for a lower bound.
    EXPECT_LE(2, llvm::count(R->Events, "start A"));
    EXPECT_LE(1, llvm::count(R->Events, "start <TU>"));
  }
  EXPECT_EQ(First->Events, Second->Events);
}

// Unqualified lookups should not be reported as qualified lookups.
TEST(ExternalSemaSource, NoQualifiedLookupNotificationsForUnqualifiedNames) {
  auto Installer = std::make_unique<ExternalSemaSourceInstaller>();
  auto Recorder = makeIntrusiveRefCnt<QualifiedLookupRecorder>();
  Installer->PushSource(Recorder);
  std::vector<std::string> Args(1, "-std=c++11");
  ASSERT_TRUE(clang::tooling::runToolOnCodeWithArgs(
      std::move(Installer), "int x; int z = x; void f() { int w = x; }", Args));
  EXPECT_TRUE(Recorder->Events.empty());
}

} // anonymous namespace
