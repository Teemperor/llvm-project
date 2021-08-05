//===- unittests/AST/ASTDumpTest.cpp --- Declaration tests ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests Decl::dump().
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclObjC.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "gtest/gtest.h"

using namespace clang;

namespace clang {
namespace ast {

namespace {
/// An ExternalASTSource that asserts if it is queried for information about
/// any declaration.
class TrappingExternalASTSource : public ExternalASTSource {
  ~TrappingExternalASTSource() override = default;
  bool FindExternalVisibleDeclsByName(const DeclContext *,
                                      DeclarationName) override {
    assert(false && "Unexpected call to FindExternalVisibleDeclsByName");
    return true;
  }

  void FindExternalLexicalDecls(const DeclContext *,
                                llvm::function_ref<bool(Decl::Kind)>,
                                SmallVectorImpl<Decl *> &) override {
    assert(false && "Unexpected call to FindExternalLexicalDecls");
  }

  void completeVisibleDeclsMap(const DeclContext *) override {
    assert(false && "Unexpected call to completeVisibleDeclsMap");
  }

  void CompleteRedeclChain(const Decl *) override {
    assert(false && "Unexpected call to CompleteRedeclChain");
  }

  void CompleteType(TagDecl *) override {
    assert(false && "Unexpected call to CompleteType(Tag Decl*)");
  }

  void CompleteType(ObjCInterfaceDecl *) override {
    assert(false && "Unexpected call to CompleteType(ObjCInterfaceDecl *)");
  }
};

/// An ExternalASTSource that records which DeclContexts were queried so far.
class RecordingExternalASTSource : public ExternalASTSource {
public:
  llvm::DenseSet<const DeclContext *> QueriedDCs;
  ~RecordingExternalASTSource() override = default;

  void FindExternalLexicalDecls(const DeclContext *DC,
                                llvm::function_ref<bool(Decl::Kind)>,
                                SmallVectorImpl<Decl *> &) override {
    QueriedDCs.insert(DC);
  }
};

class ASTDumpTestBase : public ::testing::Test {
protected:
  ASTDumpTestBase(clang::ExternalASTSource *Source)
      : FileMgr(FileMgrOpts), DiagID(new DiagnosticIDs()),
        Diags(DiagID, new DiagnosticOptions, new IgnoringDiagConsumer()),
        SourceMgr(Diags, FileMgr), Idents(LangOpts, nullptr),
        Ctxt(LangOpts, SourceMgr, Idents, Sels, Builtins, TU_Complete) {
    Ctxt.setExternalSource(Source);
  }

  FileSystemOptions FileMgrOpts;
  FileManager FileMgr;
  IntrusiveRefCntPtr<DiagnosticIDs> DiagID;
  DiagnosticsEngine Diags;
  SourceManager SourceMgr;
  LangOptions LangOpts;
  IdentifierTable Idents;
  SelectorTable Sels;
  Builtin::Context Builtins;
  ASTContext Ctxt;
};

/// Tests that Decl::dump doesn't load additional declarations from the
/// ExternalASTSource.
class NoDeserializeTest : public ASTDumpTestBase {
protected:
  NoDeserializeTest() : ASTDumpTestBase(new TrappingExternalASTSource()) {}
};

/// Tests which declarations Decl::dump deserializes;
class DeserializeTest : public ASTDumpTestBase {
protected:
  DeserializeTest()
      : ASTDumpTestBase(Recorder = new RecordingExternalASTSource()) {}
  RecordingExternalASTSource *Recorder;
};
} // unnamed namespace

/// Set all flags that activate queries to the ExternalASTSource.
static void setExternalStorageFlags(DeclContext *DC) {
  DC->setHasExternalLexicalStorage();
  DC->setHasExternalVisibleStorage();
  DC->setMustBuildLookupTable();
}

/// Dumps the given Decl.
static void dumpDecl(Decl *D, bool Deserialize) {
  // Try dumping the decl which shouldn't trigger any calls to the
  // ExternalASTSource.

  std::string Out;
  llvm::raw_string_ostream OS(Out);
  D->dump(OS, Deserialize);
}

TEST_F(NoDeserializeTest, DumpObjCInterfaceDecl) {
  // Define an Objective-C interface.
  ObjCInterfaceDecl *I = ObjCInterfaceDecl::Create(
      Ctxt, Ctxt.getTranslationUnitDecl(), SourceLocation(),
      &Ctxt.Idents.get("c"), nullptr, nullptr);
  Ctxt.getTranslationUnitDecl()->addDecl(I);

  setExternalStorageFlags(I);
  dumpDecl(I, /*Deserialize*/ false);
}

TEST_F(NoDeserializeTest, DumpRecordDecl) {
  // Define a struct.
  RecordDecl *R = RecordDecl::Create(
      Ctxt, TagDecl::TagKind::TTK_Class, Ctxt.getTranslationUnitDecl(),
      SourceLocation(), SourceLocation(), &Ctxt.Idents.get("c"));
  R->startDefinition();
  R->completeDefinition();
  Ctxt.getTranslationUnitDecl()->addDecl(R);

  setExternalStorageFlags(R);
  dumpDecl(R, /*Deserialize*/ false);
}

TEST_F(NoDeserializeTest, DumpCXXRecordDecl) {
  // Define a class.
  CXXRecordDecl *R = CXXRecordDecl::Create(
      Ctxt, TagDecl::TagKind::TTK_Class, Ctxt.getTranslationUnitDecl(),
      SourceLocation(), SourceLocation(), &Ctxt.Idents.get("c"));
  R->startDefinition();
  R->completeDefinition();
  Ctxt.getTranslationUnitDecl()->addDecl(R);

  setExternalStorageFlags(R);
  dumpDecl(R, /*Deserialize*/ false);
}

TEST_F(DeserializeTest, DumpCXXRecordDecl) {
  // Define a class.
  CXXRecordDecl *R = CXXRecordDecl::Create(
      Ctxt, TagDecl::TagKind::TTK_Class, Ctxt.getTranslationUnitDecl(),
      SourceLocation(), SourceLocation(), &Ctxt.Idents.get("c"));
  R->startDefinition();
  R->completeDefinition();
  Ctxt.getTranslationUnitDecl()->addDecl(R);

  setExternalStorageFlags(R);
  // Check that our record gets deserialized while dumping.
  EXPECT_FALSE(Recorder->QueriedDCs.contains(R));
  dumpDecl(R, /*Deserialize*/ true);
  EXPECT_TRUE(Recorder->QueriedDCs.contains(R));
}

} // end namespace ast
} // end namespace clang
