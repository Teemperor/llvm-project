//===- lldb-test.cpp ------------------------------------------ *- C++ --*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Breakpoint/BreakpointLocation.h"
#include "lldb/Core/Debugger.h"
#include "lldb/Core/Module.h"
#include "lldb/Core/Section.h"
#include "lldb/Expression/IRMemoryMap.h"
#include "lldb/Initialization/SystemLifetimeManager.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Symbol/CompileUnit.h"
#include "lldb/Symbol/LineTable.h"
#include "lldb/Symbol/SymbolFile.h"
#include "lldb/Symbol/TypeList.h"
#include "lldb/Symbol/TypeMap.h"
#include "lldb/Symbol/VariableList.h"
#include "lldb/Target/Language.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/Target.h"
#include "lldb/Utility/DataExtractor.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/State.h"
#include "lldb/Utility/StreamString.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/IntervalMap.h"
#include "llvm/ADT/ScopeExit.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/WithColor.h"

#include <cstdio>
#include <thread>
#include <fstream>

using namespace lldb;
using namespace lldb_private;
using namespace llvm;

int mainString(int argc, const char *argv[]) {
  llvm::StringMap<size_t> lengths;
  {
    std::ifstream infile(argv[2]);
    std::string line;
    size_t index = 0;
    while (std::getline(infile, line)){
        ++index;
        lengths[line] = index;
    }
  }
  {
    long result = 0;
    std::ifstream infile(argv[3]);
    std::string line;
    while (std::getline(infile, line)){
        result += lengths[line];
    }
    return result;
  }
}

int mainConst(int argc, const char *argv[]) {
  llvm::DenseMap<const char *, size_t> lengths;
  {
    std::ifstream infile(argv[2]);
    std::string line;
    size_t index = 0;
    while (std::getline(infile, line)){
        ++index;
        ConstString cline(line);
        lengths[cline.GetCString()] = index;
    }
  }
  {
    long result = 0;
    std::ifstream infile(argv[3]);
    std::string line;
    while (std::getline(infile, line)){
        ConstString cline(line);
        result += lengths[cline.GetCString()];
    }
    return result;
  }
}

int main(int argc, const char *argv[]) {
  if (llvm::StringRef(argv[1]) == "string") {
    return mainString(argc, argv);
  }
  if (llvm::StringRef(argv[1]) == "const") {
    return mainConst(argc, argv);
  }
}
