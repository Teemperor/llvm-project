//===-- ClangParserTest.cpp --------------------------------------*- C++-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Plugins/ExpressionParser/Clang/CppModuleConfiguration.h"
#include "lldb/Host/FileSystem.h"
#include "lldb/Host/HostInfo.h"

#include "gtest/gtest.h"
#include "gmock/gmock.h"

using namespace lldb_private;

namespace {
struct CppModuleConfigurationTest : public testing::Test {
  static void SetUpTestCase() {
    FileSystem::Initialize();
    HostInfo::Initialize();
  }
  static void TearDownTestCase() {
    HostInfo::Terminate();
    FileSystem::Terminate();
  }
};
} // namespace

static FileSpecList makeFiles(std::initializer_list<std::string> paths) {
  FileSpecList result;
  for (const std::string &path : paths)
    result.Append(FileSpec(path));
  return result;
}

TEST_F(CppModuleConfigurationTest, Basic) {
  CppModuleConfiguration config(makeFiles({"/usr/include/bits/types.h",
                                           "/usr/include/c++/v1/vector"}));
  const auto &imported_modules = config.GetImportedModules();
  EXPECT_THAT(imported_modules, testing::ElementsAre("std"));
  EXPECT_THAT(config.GetIncludeDirs(), testing::ElementsAre("/usr/include/",
                                                            "",
                                                            "/usr/include/c++/v1/"));
}


TEST_F(CppModuleConfigurationTest, MissingUsr) {
  CppModuleConfiguration config(makeFiles({"/usr/include/c++/v1/vector"}));
  const auto &imported_modules = config.GetImportedModules();
  EXPECT_THAT(imported_modules, testing::ElementsAre());
}


TEST_F(CppModuleConfigurationTest, MissingLibCpp) {
  CppModuleConfiguration config(makeFiles({"/usr/include/bits/types.h"}));
  const auto &imported_modules = config.GetImportedModules();
  EXPECT_THAT(imported_modules, testing::ElementsAre());
}
