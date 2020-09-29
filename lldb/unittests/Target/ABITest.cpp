//===-- ABITest.cpp -------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Target/ABI.h"
#include "lldb/Utility/ArchSpec.h"
#include "Plugins/ABI/AArch64/ABIMacOSX_arm64.h"
#include "gtest/gtest.h"
#include "TestingSupport/SubsystemRAII.h"
#include "llvm/Support/TargetSelect.h"

using namespace lldb_private;

TEST(MCBasedABI, MapRegisterName) {
  auto map = [](std::string name) {
    MCBasedABI::MapRegisterName(name, "foo", "bar");
    return name;
  };
  EXPECT_EQ("bar", map("foo"));
  EXPECT_EQ("bar0", map("foo0"));
  EXPECT_EQ("bar47", map("foo47"));
  EXPECT_EQ("foo47x", map("foo47x"));
  EXPECT_EQ("fooo47", map("fooo47"));
  EXPECT_EQ("bar47", map("bar47"));
}


TEST(ABIMacOSX_arm64, RegisterIsVolatile) {
  SubsystemRAII<ABIMacOSX_arm64> subsystems;

  llvm::InitializeAllTargets();
  llvm::InitializeAllAsmPrinters();
  llvm::InitializeAllTargetMCs();
  llvm::InitializeAllDisassemblers();

  ArchSpec spec("arm64-apple-macosx");
  lldb::ABISP abi = ABIMacOSX_arm64::CreateInstance(nullptr, spec);

  std::vector<std::pair<std::string, bool>> registers_and_volatility = {
    {"x18", true},
    {"x0", true},
    {"x19", false},
    {"x29", false},
    {"x30", true},
    {"v0", true},
    {"v7", true},
    {"v8", false},
    {"v10", false},
    {"v15", false},
    {"v16", true},
    {"v31", true},
  };
  for (const auto &reg_and_vol : registers_and_volatility) {
    SCOPED_TRACE(reg_and_vol.first);
    RegisterInfo info;
    info.name = reg_and_vol.first.c_str();
    EXPECT_EQ(abi->RegisterIsVolatile(&info), reg_and_vol.second);
  }
}
