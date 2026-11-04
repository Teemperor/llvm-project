//===-- TimerCategories.td - LLDB timer categories ---------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// All categories that can be used with LLDB_SCOPED_TIMER (see Timer.h).
//
// A category name consists of dot-separated components that determine the
// nesting of the timer in the "statistics dump" JSON. Every component must be
// a C++ identifier, because the category "A.B" is available in C++ as the
// constant lldb_private::timers::A::B (see LLDB_SCOPED_TIMER). By convention
// the name is "Class.Method". The build fails if a name is defined twice, or
// if a category is nested in another category (e.g., "A.B" and "A.B.C").
//
//===----------------------------------------------------------------------===//

class TimerCategory<string name> {
  string Name = name;
}

def : TimerCategory<"AppleObjCRuntimeV2.UpdateISAToDescriptorMapIfNeeded">;
def : TimerCategory<"CommandInterpreter.HandleCommand">;
def : TimerCategory<"CommandInterpreter.Initialize">;
def : TimerCategory<"CommandInterpreter.LoadCommandDictionary">;
def : TimerCategory<"CommandObjectTargetCreate.DoExecute">;
def : TimerCategory<"CompileUnit.FindFunction">;
def : TimerCategory<"CPPLanguageRuntime.FindLibCppStdFunctionCallableInfo">;
def : TimerCategory<"Disassembler.FindPlugin">;
def : TimerCategory<"DWARFCallFrameInfo.GetFDEIndex">;
def : TimerCategory<"DWARFDebugAranges.Sort">;
def : TimerCategory<"DWARFUnit.ExtractDIEsRWLocked">;
def : TimerCategory<"HostInfoMacOSX.find_cached_path">;
def : TimerCategory<"ManualDWARFIndex.Index">;
def : TimerCategory<"Module.FindFirstSymbolWithNameAndType">;
def : TimerCategory<"Module.FindFunctionSymbols">;
def : TimerCategory<"Module.FindSymbolsMatchingRegExAndType">;
def : TimerCategory<"Module.GetObjectFile">;
def : TimerCategory<"Module.GetSymbolFile">;
def : TimerCategory<"Module.ResolveSymbolContextsForFileSpec">;
def : TimerCategory<"ObjectContainer.FindPlugin">;
def : TimerCategory<"ObjectContainerBSDArchive.CreateInstance">;
def : TimerCategory<"ObjectFile.FindPlugin">;
def : TimerCategory<"ObjectFile.FindPluginInMemory">;
def : TimerCategory<"ObjectFileELF.CalculateCRC32">;
def : TimerCategory<"ObjectFileMachO.ParseSymtab">;
def : TimerCategory<"ObjectFilePECOFF.GetCoffUUID">;
def : TimerCategory<"Process.ReadMemoryFromInferior">;
def : TimerCategory<"ScriptInterpreterLua.ExecuteInterpreterLoop">;
def : TimerCategory<"ScriptInterpreterPython.ExecuteInterpreterLoop">;
def : TimerCategory<"ScriptInterpreterPython.GetScriptedSummary">;
def : TimerCategory<"ScriptInterpreterPython.Initialize">;
def : TimerCategory<"ScriptInterpreterPython.LLDBSwigPythonCallTypeScript">;
def : TimerCategory<"SymbolFileDWARF.DebugInfo">;
def : TimerCategory<"SymbolFileDWARF.FindFunctionsByRegex">;
def : TimerCategory<"SymbolFileDWARF.ParseFunctions">;
def : TimerCategory<"SymbolFileDWARF.ResolveSymbolContext">;
def : TimerCategory<"SymbolFileDWARFDebugMap.FindFunctions">;
def : TimerCategory<"SymbolFileDWARFDebugMap.FindFunctionsByRegex">;
def : TimerCategory<"SymbolFileDWARFDebugMap.GetTypes">;
def : TimerCategory<"SymbolLocatorDebugSymbols.LocateExecutableSymbolFile">;
def : TimerCategory<"SymbolLocatorDefault.LocateExecutableObjectFile">;
def : TimerCategory<"SymbolVendorELF.CreateInstance">;
def : TimerCategory<"SymbolVendorMacOSX.CreateInstance">;
def : TimerCategory<"SymbolVendorMacOSX.LocateDSYM">;
def : TimerCategory<"SymbolVendorPECOFF.CreateInstance">;
def : TimerCategory<"SymbolVendorWasm.CreateInstance">;
def : TimerCategory<"Symtab.AppendSymbolIndexesWithName">;
def : TimerCategory<"Symtab.AppendSymbolNamesToMap">;
def : TimerCategory<"Symtab.FindAllSymbolsWithNameAndType">;
def : TimerCategory<"Symtab.FindFirstSymbolWithNameAndType">;
def : TimerCategory<"Symtab.InitNameIndexes">;
def : TimerCategory<"Symtab.SortSymbolIndexesByValue">;
def : TimerCategory<"SystemInitializerCommon.Initialize">;
def : TimerCategory<"SystemInitializerCommon.Terminate">;
def : TimerCategory<"Target.RebuildModuleListWithExecutable">;
def : TimerCategory<"TargetList.CreateTarget">;

// Only used by unittests/Utility/TimerTest.cpp.
def : TimerCategory<"TimerTest.A">;
def : TimerCategory<"TimerTest.B">;
