//===- LLDBOptionDefEmitter.cpp - Generate LLDB command options =-*- C++ -*--=//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// These tablegen backends emits LLDB's OptionDefinition values for different
// LLDB commands.
//
//===----------------------------------------------------------------------===//

#include "LLDBTableGenBackends.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/StringMatcher.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <vector>

using namespace llvm;

static void emitOption(Record *Option, raw_ostream &OS) {
  OS << "{LLDB_OPT_SET_ALL,  ";

  if (auto R = Option->getValue("Required"))
    OS << "true";
  else
    OS << "false";

  OS << ", \"" << Option->getValueAsString("FullName") << "\", ";
  OS << '\'' << Option->getValueAsString("ShortName") << "'";

  auto ArgType = Option->getValue("ArgType");
  bool IsOptionalArg = Option->getValue("OptionalArg") != nullptr;

  OS << ", OptionParser::";
  if (ArgType) {
    if (IsOptionalArg)
      OS << "eOptionalArgument";
    else
      OS << "eRequiredArgument";
  } else
    OS << "eNoArgument";
  OS << ", nullptr, nullptr, ";

  if (Option->getValue("Completions")) {
    auto Completions = Option->getValueAsListOfStrings("Completions");
    std::vector<std::string> CompletionArgs;
    for (auto Completion : Completions)
      CompletionArgs.push_back("CommandCompletions::e" + Completion.str() + "Completion");
    OS << llvm::join(CompletionArgs.begin(), CompletionArgs.end(), " | ");
  } else {
    OS << "CommandCompletions::eNoCompletion";
  }

  OS << ", eArgType";
  if (ArgType) {
    OS << ArgType->getValue()->getAsUnquotedString();
  } else
    OS << "None";
  OS << ", ";

  if (auto D = Option->getValue("Description"))
    OS << D->getValue()->getAsString();
  else
    OS << "\"\"";
  OS << "},\n";
}

void lldb_private::EmitOptionDefs(RecordKeeper &Records, raw_ostream &OS) {

  std::vector<Record *> Options = Records.getAllDerivedDefinitions("Option");

  emitSourceFileHeader("Description for help", OS);
  for (Record *Option : Options) {
    emitOption(Option, OS);
  }
}
