//===-- SystemInitializerAllPlugins.cpp -----------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemInitializerTest.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Host/Host.h"
#include "lldb/Initialization/SystemInitializerAllPlugins.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Utility/Timer.h"

using namespace lldb_private;

SystemInitializerTest::SystemInitializerTest() {}

SystemInitializerTest::~SystemInitializerTest() {}

llvm::Error SystemInitializerTest::Initialize() {
  if (auto e = SystemInitializerAllPlugins::Initialize())
    return e;

  return llvm::Error::success();
}

void SystemInitializerTest::Terminate() {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, LLVM_PRETTY_FUNCTION);

  SystemInitializerAllPlugins::Terminate();
}
