//===-- SystemInitializerFull.cpp -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SystemInitializerFull.h"
#include "lldb/API/SBCommandInterpreter.h"
#include "lldb/Host/Config.h"
#include "lldb/Utility/Timer.h"

#if LLDB_ENABLE_PYTHON
#include "Plugins/OperatingSystem/Python/OperatingSystemPython.h"
#include "Plugins/ScriptInterpreter/Python/ScriptInterpreterPython.h"
#endif

#if LLDB_ENABLE_LUA
#include "Plugins/ScriptInterpreter/Lua/ScriptInterpreterLua.h"
#endif

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wglobal-constructors"
#include "llvm/ExecutionEngine/MCJIT.h"
#pragma clang diagnostic pop

using namespace lldb_private;

SystemInitializerFull::SystemInitializerFull() {}

SystemInitializerFull::~SystemInitializerFull() {}

llvm::Error SystemInitializerFull::Initialize() {
  if (auto e = SystemInitializerAllPlugins::Initialize())
    return e;

#if LLDB_ENABLE_PYTHON
  OperatingSystemPython::Initialize();
  ScriptInterpreterPython::Initialize();
#endif

#if LLDB_ENABLE_LUA
  ScriptInterpreterLua::Initialize();
#endif



  return llvm::Error::success();
}

void SystemInitializerFull::Terminate() {
  static Timer::Category func_cat(LLVM_PRETTY_FUNCTION);
  Timer scoped_timer(func_cat, LLVM_PRETTY_FUNCTION);

  SystemInitializerAllPlugins::Terminate();

#if LLDB_ENABLE_LUA
  ScriptInterpreterLua::Terminate();
#endif

#if LLDB_ENABLE_PYTHON
  ScriptInterpreterPython::Terminate();
  OperatingSystemPython::Terminate();
#endif
}
