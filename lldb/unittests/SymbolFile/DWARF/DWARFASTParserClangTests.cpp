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
class DWARFASTParserClangTests : public testing::Test {};

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
TEST_F(DWARFASTParserClangTests,
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
      "  - Version:         4\n"
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

  /// Takes the given integer value, stores it in a DWARFFormValue and then
  /// tries to extract the value back via
  /// DWARFASTParserClang::ExtractIntFromFormValue.
  /// Returns the string representation of the extracted value or the error
  /// that was returned from ExtractIntFromFormValue.
  llvm::Expected<std::string> Extract(clang::QualType qt, uint64_t value) {
    DWARFFormValue form_value;
    form_value.SetUnsigned(value);
    llvm::Expected<llvm::APInt> result =
        parser.ExtractIntFromFormValue(ts.GetType(qt), form_value);
    if (!result)
      return result.takeError();
    llvm::SmallString<16> result_str;
    result->toStringUnsigned(result_str);
    return std::string(result_str.str());
  }

  /// Same as ExtractIntFromFormValueTest::Extract but takes a signed integer
  /// and treats the result as a signed integer.
  llvm::Expected<std::string> ExtractS(clang::QualType qt, int64_t value) {
    DWARFFormValue form_value;
    form_value.SetSigned(value);
    llvm::Expected<llvm::APInt> result =
        parser.ExtractIntFromFormValue(ts.GetType(qt), form_value);
    if (!result)
      return result.takeError();
    llvm::SmallString<16> result_str;
    result->toStringSigned(result_str);
    return std::string(result_str.str());
  }
};

TEST_F(ExtractIntFromFormValueTest, TestBool) {
  using namespace llvm;
  clang::ASTContext &ast = ts.getASTContext();

  EXPECT_THAT_EXPECTED(Extract(ast.BoolTy, 0), HasValue("0"));
  EXPECT_THAT_EXPECTED(Extract(ast.BoolTy, 1), HasValue("1"));
  EXPECT_THAT_EXPECTED(Extract(ast.BoolTy, 2), Failed());
  EXPECT_THAT_EXPECTED(Extract(ast.BoolTy, 3), Failed());
}

TEST_F(ExtractIntFromFormValueTest, TestInt) {
  using namespace llvm;

  clang::ASTContext &ast = ts.getASTContext();

  // Find the min/max values for 'int' on the current host target.
  const int64_t int_max = std::numeric_limits<int>::max();
  const int64_t int_min = std::numeric_limits<int>::min();

  // Check that the bit width of int matches the int width in our type system.
  ASSERT_EQ(sizeof(int) * 8, ast.getIntWidth(ast.IntTy));

  // Check values around int_min.
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_min - 2), llvm::Failed());
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_min - 1), llvm::Failed());
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_min),
                       HasValue(std::to_string(int_min)));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_min + 1),
                       HasValue(std::to_string(int_min + 1)));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_min + 2),
                       HasValue(std::to_string(int_min + 2)));

  // Check values around 0.
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, -128), HasValue("-128"));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, -10), HasValue("-10"));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, -1), HasValue("-1"));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, 0), HasValue("0"));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, 1), HasValue("1"));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, 10), HasValue("10"));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, 128), HasValue("128"));

  // Check values around int_max.
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_max - 2),
                       HasValue(std::to_string(int_max - 2)));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_max - 1),
                       HasValue(std::to_string(int_max - 1)));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_max),
                       HasValue(std::to_string(int_max)));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_max + 1), llvm::Failed());
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_max + 5), llvm::Failed());

  // Check some values not near an edge case.
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_max / 2),
                       HasValue(std::to_string(int_max / 2)));
  EXPECT_THAT_EXPECTED(ExtractS(ast.IntTy, int_min / 2),
                       HasValue(std::to_string(int_min / 2)));
}

TEST_F(ExtractIntFromFormValueTest, TestUnsignedInt) {
  using namespace llvm;

  clang::ASTContext &ast = ts.getASTContext();
  const uint64_t uint_max =
      (static_cast<uint64_t>(1) << ast.getIntWidth(ast.UnsignedIntTy)) - 1U;

  // Check values around 0.
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, 0), HasValue("0"));
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, 1), HasValue("1"));
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, 1234), HasValue("1234"));

  // Check some values not near an edge case.
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, uint_max / 2),
                       HasValue(std::to_string(uint_max / 2)));

  // Check values around uint_max.
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, uint_max - 2),
                       HasValue(std::to_string(uint_max - 2)));
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, uint_max - 1),
                       HasValue(std::to_string(uint_max - 1)));
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, uint_max),
                       HasValue(std::to_string(uint_max)));
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, uint_max + 1),
                       llvm::Failed());
  EXPECT_THAT_EXPECTED(Extract(ast.UnsignedIntTy, uint_max + 2),
                       llvm::Failed());
}
