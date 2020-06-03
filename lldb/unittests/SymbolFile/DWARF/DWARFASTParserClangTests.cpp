//===-- DWARFASTParserClangTests.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/SymbolFile/DWARF/DWARFASTParserClang.h"
#include "Plugins/SymbolFile/DWARF/DWARFCompileUnit.h"
#include "Plugins/SymbolFile/DWARF/DWARFDIE.h"
#include "TestingSupport/Symbol/YAMLModuleTester.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using namespace lldb;
using namespace lldb_private;

namespace {
class DWARFASTParserClangStubTests : public testing::Test {};

class DWARFASTParserClangStub : public DWARFASTParserClang {
public:
  using DWARFASTParserClang::DWARFASTParserClang;
  using DWARFASTParserClang::LinkDeclContextToDIE;

  std::vector<const clang::DeclContext *> GetDeclContextToDIEMapKeys() {
    std::vector<const clang::DeclContext *> keys;
    for (const auto &it : m_decl_ctx_to_die)
      keys.push_back(it.first);
    return keys;
  }
};
} // namespace

// If your implementation needs to dereference the dummy pointers we are
// defining here, causing this test to fail, feel free to delete it.
TEST_F(DWARFASTParserClangStubTests,
       EnsureAllDIEsInDeclContextHaveBeenParsedParsesOnlyMatchingEntries) {

  /// Auxiliary debug info.
  const char *yamldata =
      "debug_abbrev:\n"
      "  - Code:            0x00000001\n"
      "    Tag:             DW_TAG_compile_unit\n"
      "    Children:        DW_CHILDREN_yes\n"
      "    Attributes:\n"
      "      - Attribute:       DW_AT_language\n"
      "        Form:            DW_FORM_data2\n"
      "  - Code:            0x00000002\n"
      "    Tag:             DW_TAG_base_type\n"
      "    Children:        DW_CHILDREN_no\n"
      "    Attributes:\n"
      "      - Attribute:       DW_AT_encoding\n"
      "        Form:            DW_FORM_data1\n"
      "      - Attribute:       DW_AT_byte_size\n"
      "        Form:            DW_FORM_data1\n"
      "debug_info:\n"
      "  - Length:\n"
      "      TotalLength:     0\n"
      "    Version:         4\n"
      "    AbbrOffset:      0\n"
      "    AddrSize:        8\n"
      "    Entries:\n"
      "      - AbbrCode:        0x00000001\n"
      "        Values:\n"
      "          - Value:           0x000000000000000C\n"
      // 0x0000000e:
      "      - AbbrCode:        0x00000002\n"
      "        Values:\n"
      "          - Value:           0x0000000000000007\n" // DW_ATE_unsigned
      "          - Value:           0x0000000000000004\n"
      // 0x00000011:
      "      - AbbrCode:        0x00000002\n"
      "        Values:\n"
      "          - Value:           0x0000000000000007\n" // DW_ATE_unsigned
      "          - Value:           0x0000000000000008\n"
      // 0x00000014:
      "      - AbbrCode:        0x00000002\n"
      "        Values:\n"
      "          - Value:           0x0000000000000005\n" // DW_ATE_signed
      "          - Value:           0x0000000000000008\n"
      // 0x00000017:
      "      - AbbrCode:        0x00000002\n"
      "        Values:\n"
      "          - Value:           0x0000000000000008\n" // DW_ATE_unsigned_char
      "          - Value:           0x0000000000000001\n"
      ""
      "      - AbbrCode:        0x00000000\n"
      "        Values:          []\n";

  YAMLModuleTester t(yamldata, "i386-unknown-linux");
  ASSERT_TRUE((bool)t.GetDwarfUnit());

  TypeSystemClang ast_ctx("dummy ASTContext", HostInfoBase::GetTargetTriple());
  DWARFASTParserClangStub ast_parser(ast_ctx);

  DWARFUnit *unit = t.GetDwarfUnit().get();
  const DWARFDebugInfoEntry *die_first = unit->DIE().GetDIE();
  const DWARFDebugInfoEntry *die_child0 = die_first->GetFirstChild();
  const DWARFDebugInfoEntry *die_child1 = die_child0->GetSibling();
  const DWARFDebugInfoEntry *die_child2 = die_child1->GetSibling();
  const DWARFDebugInfoEntry *die_child3 = die_child2->GetSibling();
  std::vector<DWARFDIE> dies = {
      DWARFDIE(unit, die_child0), DWARFDIE(unit, die_child1),
      DWARFDIE(unit, die_child2), DWARFDIE(unit, die_child3)};
  std::vector<clang::DeclContext *> decl_ctxs = {
      (clang::DeclContext *)1LL, (clang::DeclContext *)2LL,
      (clang::DeclContext *)2LL, (clang::DeclContext *)3LL};
  for (int i = 0; i < 4; ++i)
    ast_parser.LinkDeclContextToDIE(decl_ctxs[i], dies[i]);
  ast_parser.EnsureAllDIEsInDeclContextHaveBeenParsed(
      CompilerDeclContext(nullptr, decl_ctxs[1]));

  EXPECT_THAT(ast_parser.GetDeclContextToDIEMapKeys(),
              testing::UnorderedElementsAre(decl_ctxs[0], decl_ctxs[3]));
}

struct ExtractIntFromFormValueTest : public testing::Test {
  SubsystemRAII<FileSystem, HostInfo> subsystems;
  TypeSystemClang ts;
  DWARFASTParserClang parser;
  ExtractIntFromFormValueTest()
      : ts("dummy ASTContext", HostInfoBase::GetTargetTriple()), parser(ts) {}

  llvm::Expected<llvm::APInt> ExtractUnsigned(clang::QualType qt,
                                              uint64_t value) {
    DWARFFormValue form_value;
    form_value.SetUnsigned(value);
    return parser.ExtractIntFromFormValue(ts.GetType(qt), form_value);
  }

  std::string ExtractUnsignedValue(clang::QualType qt, uint64_t value) {
    llvm::Expected<llvm::APInt> err = ExtractUnsigned(qt, value);
    EXPECT_THAT_ERROR(err.takeError(), llvm::Succeeded());
    llvm::SmallString<16> result;
    err->toStringUnsigned(result);
    return std::string(result.str());
  }

  llvm::Expected<llvm::APInt> ExtractSigned(clang::QualType qt, int64_t value) {
    DWARFFormValue form_value;
    form_value.SetSigned(value);
    return parser.ExtractIntFromFormValue(ts.GetType(qt), form_value);
  }

  std::string ExtractSignedValue(clang::QualType qt, int64_t value) {
    llvm::Expected<llvm::APInt> err = ExtractSigned(qt, value);
    EXPECT_THAT_ERROR(err.takeError(), llvm::Succeeded());
    llvm::SmallString<16> result;
    err->toStringSigned(result);
    return std::string(result.str());
  }
};

TEST_F(ExtractIntFromFormValueTest, TestBool) {
  clang::ASTContext &ast = ts.getASTContext();
  EXPECT_EQ(ExtractUnsignedValue(ast.BoolTy, 0), "0");
  EXPECT_EQ(ExtractUnsignedValue(ast.BoolTy, 1), "1");
  EXPECT_THAT_ERROR(ExtractUnsigned(ast.BoolTy, 2).takeError(), llvm::Failed());
  EXPECT_THAT_ERROR(ExtractUnsigned(ast.BoolTy, 3).takeError(), llvm::Failed());
}

TEST_F(ExtractIntFromFormValueTest, TestInt) {
  clang::ASTContext &ast = ts.getASTContext();
  const int64_t int_max = static_cast<uint64_t>(1)
                          << (ast.getIntWidth(ast.IntTy) - 1) - 1;
  const int64_t int_min = -int_max - 1;

  EXPECT_THAT_ERROR(ExtractSigned(ast.IntTy, int_min - 2).takeError(),
                    llvm::Failed());
  EXPECT_THAT_ERROR(ExtractSigned(ast.IntTy, int_min - 1).takeError(),
                    llvm::Failed());
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, int_min), std::to_string(int_min));
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, int_min + 1),
            std::to_string(int_min + 1));
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, int_min + 2),
            std::to_string(int_min + 2));
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, -10), "-10");
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, -1), "-1");
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, 0), "0");
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, 1), "1");
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, 10), "10");
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, int_max - 2),
            std::to_string(int_max - 2));
  EXPECT_EQ(ExtractSignedValue(ast.IntTy, int_max - 1),
            std::to_string(int_max - 1));
  EXPECT_THAT_ERROR(ExtractSigned(ast.IntTy, int_max - 2).takeError(),
                    llvm::Succeeded());
  EXPECT_THAT_ERROR(ExtractSigned(ast.IntTy, int_max - 1).takeError(),
                    llvm::Succeeded());
  EXPECT_THAT_ERROR(ExtractSigned(ast.IntTy, int_max).takeError(),
                    llvm::Succeeded());
  EXPECT_THAT_ERROR(ExtractSigned(ast.IntTy, int_max + 1).takeError(),
                    llvm::Failed());
  EXPECT_THAT_ERROR(ExtractSigned(ast.IntTy, int_max + 5).takeError(),
                    llvm::Failed());
}

TEST_F(ExtractIntFromFormValueTest, TestUnsignedInt) {
  clang::ASTContext &ast = ts.getASTContext();
  const uint64_t uint_max =
      (static_cast<uint64_t>(1) << ast.getIntWidth(ast.UnsignedIntTy)) - 1U;

  EXPECT_EQ(ExtractUnsignedValue(ast.UnsignedIntTy, 0), "0");
  EXPECT_EQ(ExtractUnsignedValue(ast.UnsignedIntTy, 1), "1");
  EXPECT_EQ(ExtractUnsignedValue(ast.UnsignedIntTy, 1234), "1234");
  EXPECT_EQ(ExtractUnsignedValue(ast.UnsignedIntTy, uint_max - 1),
            std::to_string(uint_max - 1));
  EXPECT_EQ(ExtractUnsignedValue(ast.UnsignedIntTy, uint_max),
            std::to_string(uint_max));
  EXPECT_THAT_ERROR(
      ExtractUnsigned(ast.UnsignedIntTy, uint_max + 1).takeError(),
      llvm::Failed());
}
