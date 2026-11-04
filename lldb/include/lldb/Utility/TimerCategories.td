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
// constant lldb_private::timers::A::B (see LLDB_SCOPED_TIMER). Components that
// are C++ keywords get a trailing underscore in C++ (e.g., "A.delete" is
// lldb_private::timers::A::delete_). By convention the name is
// "Class.Method". The build fails if a name is defined twice, or if a category
// is nested in another category (e.g., "A.B" and "A.B.C").
//
//===----------------------------------------------------------------------===//

class TimerCategory<string name> {
  string Name = name;
}

// The category of an LLDB command, timed whenever the command executes (see
// CommandObjectParsed::Execute and CommandObjectRaw::Execute). The argument is
// the full name of the command as returned by CommandObject::GetCommandName.
// The category name is the command name prefixed with "Command.", where spaces
// separate components and '-' is replaced by '_' (e.g., the command
// "thread step-in" has the category "Command.thread.step_in").
class CommandTimer<string command>
    : TimerCategory<"Command." # !subst("-", "_", !subst(" ", ".", command))> {
  string Command = command;
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

// Built-in commands. CommandTimerTest in unittests/Interpreter checks that
// every built-in command is listed. Commands that are only added by plugins
// (e.g., "process plugin packet send") have to be added manually.
def : CommandTimer<"_regexp-attach">;
def : CommandTimer<"_regexp-break">;
def : CommandTimer<"_regexp-break-add">;
def : CommandTimer<"_regexp-bt">;
def : CommandTimer<"_regexp-display">;
def : CommandTimer<"_regexp-down">;
def : CommandTimer<"_regexp-env">;
def : CommandTimer<"_regexp-jump">;
def : CommandTimer<"_regexp-list">;
def : CommandTimer<"_regexp-step">;
def : CommandTimer<"_regexp-tbreak">;
def : CommandTimer<"_regexp-undisplay">;
def : CommandTimer<"_regexp-up">;
def : CommandTimer<"apropos">;
def : CommandTimer<"breakpoint add address">;
def : CommandTimer<"breakpoint add exception">;
def : CommandTimer<"breakpoint add file">;
def : CommandTimer<"breakpoint add name">;
def : CommandTimer<"breakpoint add pattern">;
def : CommandTimer<"breakpoint add scripted">;
def : CommandTimer<"breakpoint clear">;
def : CommandTimer<"breakpoint command add">;
def : CommandTimer<"breakpoint command delete">;
def : CommandTimer<"breakpoint command list">;
def : CommandTimer<"breakpoint delete">;
def : CommandTimer<"breakpoint disable">;
def : CommandTimer<"breakpoint enable">;
def : CommandTimer<"breakpoint list">;
def : CommandTimer<"breakpoint modify">;
def : CommandTimer<"breakpoint name add">;
def : CommandTimer<"breakpoint name configure">;
def : CommandTimer<"breakpoint name delete">;
def : CommandTimer<"breakpoint name list">;
def : CommandTimer<"breakpoint override add">;
def : CommandTimer<"breakpoint override delete">;
def : CommandTimer<"breakpoint override list">;
def : CommandTimer<"breakpoint read">;
def : CommandTimer<"breakpoint set">;
def : CommandTimer<"breakpoint write">;
def : CommandTimer<"command alias">;
def : CommandTimer<"command container add">;
def : CommandTimer<"command container delete">;
def : CommandTimer<"command delete">;
def : CommandTimer<"command regex">;
def : CommandTimer<"command script add">;
def : CommandTimer<"command script clear">;
def : CommandTimer<"command script delete">;
def : CommandTimer<"command script import">;
def : CommandTimer<"command script list">;
def : CommandTimer<"command source">;
def : CommandTimer<"command unalias">;
def : CommandTimer<"diagnostics dump">;
def : CommandTimer<"diagnostics report">;
def : CommandTimer<"disassemble">;
def : CommandTimer<"dwim-print">;
def : CommandTimer<"expression">;
def : CommandTimer<"frame diagnose">;
def : CommandTimer<"frame info">;
def : CommandTimer<"frame recognizer add">;
def : CommandTimer<"frame recognizer clear">;
def : CommandTimer<"frame recognizer delete">;
def : CommandTimer<"frame recognizer disable">;
def : CommandTimer<"frame recognizer enable">;
def : CommandTimer<"frame recognizer info">;
def : CommandTimer<"frame recognizer list">;
def : CommandTimer<"frame select">;
def : CommandTimer<"frame variable">;
def : CommandTimer<"gdb-remote">;
def : CommandTimer<"gui">;
def : CommandTimer<"help">;
def : CommandTimer<"kdp-remote">;
def : CommandTimer<"language cplusplus demangle">;
def : CommandTimer<"language objc class-table dump">;
def : CommandTimer<"language objc tagged-pointer info">;
def : CommandTimer<"log disable">;
def : CommandTimer<"log dump">;
def : CommandTimer<"log enable">;
def : CommandTimer<"log list">;
def : CommandTimer<"log timers disable">;
def : CommandTimer<"log timers dump">;
def : CommandTimer<"log timers enable">;
def : CommandTimer<"log timers increment">;
def : CommandTimer<"log timers reset">;
def : CommandTimer<"memory find">;
def : CommandTimer<"memory history">;
def : CommandTimer<"memory read">;
def : CommandTimer<"memory region">;
def : CommandTimer<"memory tag read">;
def : CommandTimer<"memory tag write">;
def : CommandTimer<"memory write">;
def : CommandTimer<"platform connect">;
def : CommandTimer<"platform disconnect">;
def : CommandTimer<"platform file close">;
def : CommandTimer<"platform file open">;
def : CommandTimer<"platform file read">;
def : CommandTimer<"platform file write">;
def : CommandTimer<"platform file-exists">;
def : CommandTimer<"platform get-file">;
def : CommandTimer<"platform get-permissions">;
def : CommandTimer<"platform get-size">;
def : CommandTimer<"platform list">;
def : CommandTimer<"platform mkdir">;
def : CommandTimer<"platform process attach">;
def : CommandTimer<"platform process info">;
def : CommandTimer<"platform process launch">;
def : CommandTimer<"platform process list">;
def : CommandTimer<"platform put-file">;
def : CommandTimer<"platform select">;
def : CommandTimer<"platform settings">;
def : CommandTimer<"platform shell">;
def : CommandTimer<"platform status">;
def : CommandTimer<"platform target-install">;
def : CommandTimer<"plugin disable">;
def : CommandTimer<"plugin enable">;
def : CommandTimer<"plugin list">;
def : CommandTimer<"plugin load">;
def : CommandTimer<"plugin structured-data darwin-log disable">;
def : CommandTimer<"plugin structured-data darwin-log enable">;
def : CommandTimer<"plugin structured-data darwin-log status">;
def : CommandTimer<"process attach">;
def : CommandTimer<"process connect">;
def : CommandTimer<"process continue">;
def : CommandTimer<"process detach">;
def : CommandTimer<"process handle">;
def : CommandTimer<"process interrupt">;
def : CommandTimer<"process kill">;
def : CommandTimer<"process launch">;
def : CommandTimer<"process load">;
def : CommandTimer<"process plugin dump">;
def : CommandTimer<"process plugin packet history">;
def : CommandTimer<"process plugin packet monitor">;
def : CommandTimer<"process plugin packet send">;
def : CommandTimer<"process plugin packet speed-test">;
def : CommandTimer<"process plugin packet xfer-size">;
def : CommandTimer<"process plugin refresh-threads">;
def : CommandTimer<"process save-core">;
def : CommandTimer<"process signal">;
def : CommandTimer<"process status">;
def : CommandTimer<"process trace start">;
def : CommandTimer<"process trace stop">;
def : CommandTimer<"process unload">;
def : CommandTimer<"protocol-server get">;
def : CommandTimer<"protocol-server start">;
def : CommandTimer<"protocol-server stop">;
def : CommandTimer<"quit">;
def : CommandTimer<"register info">;
def : CommandTimer<"register read">;
def : CommandTimer<"register write">;
def : CommandTimer<"scripting extension generate">;
def : CommandTimer<"scripting extension list">;
def : CommandTimer<"scripting run">;
def : CommandTimer<"session history">;
def : CommandTimer<"session save">;
def : CommandTimer<"settings append">;
def : CommandTimer<"settings clear">;
def : CommandTimer<"settings insert-after">;
def : CommandTimer<"settings insert-before">;
def : CommandTimer<"settings list">;
def : CommandTimer<"settings read">;
def : CommandTimer<"settings remove">;
def : CommandTimer<"settings replace">;
def : CommandTimer<"settings set">;
def : CommandTimer<"settings show">;
def : CommandTimer<"settings write">;
def : CommandTimer<"source cache clear">;
def : CommandTimer<"source cache dump">;
def : CommandTimer<"source info">;
def : CommandTimer<"source list">;
def : CommandTimer<"statistics disable">;
def : CommandTimer<"statistics dump">;
def : CommandTimer<"statistics enable">;
def : CommandTimer<"target create">;
def : CommandTimer<"target delete">;
def : CommandTimer<"target dump section-load-list">;
def : CommandTimer<"target dump typesystem">;
def : CommandTimer<"target frame-provider clear">;
def : CommandTimer<"target frame-provider list">;
def : CommandTimer<"target frame-provider register">;
def : CommandTimer<"target frame-provider remove">;
def : CommandTimer<"target groups list">;
def : CommandTimer<"target hook add">;
def : CommandTimer<"target hook delete">;
def : CommandTimer<"target hook disable">;
def : CommandTimer<"target hook enable">;
def : CommandTimer<"target hook list">;
def : CommandTimer<"target hook modify">;
def : CommandTimer<"target list">;
def : CommandTimer<"target modules add">;
def : CommandTimer<"target modules dump ast">;
def : CommandTimer<"target modules dump line-table">;
def : CommandTimer<"target modules dump objfile">;
def : CommandTimer<"target modules dump pcm-info">;
def : CommandTimer<"target modules dump sections">;
def : CommandTimer<"target modules dump separate-debug-info">;
def : CommandTimer<"target modules dump symfile">;
def : CommandTimer<"target modules dump symtab">;
def : CommandTimer<"target modules list">;
def : CommandTimer<"target modules load">;
def : CommandTimer<"target modules lookup">;
def : CommandTimer<"target modules search-paths add">;
def : CommandTimer<"target modules search-paths clear">;
def : CommandTimer<"target modules search-paths insert">;
def : CommandTimer<"target modules search-paths list">;
def : CommandTimer<"target modules search-paths query">;
def : CommandTimer<"target modules show-unwind">;
def : CommandTimer<"target select">;
def : CommandTimer<"target show-launch-environment">;
def : CommandTimer<"target stop-hook add">;
def : CommandTimer<"target stop-hook delete">;
def : CommandTimer<"target stop-hook disable">;
def : CommandTimer<"target stop-hook enable">;
def : CommandTimer<"target stop-hook list">;
def : CommandTimer<"target symbols add">;
def : CommandTimer<"target variable">;
def : CommandTimer<"thread backtrace">;
def : CommandTimer<"thread continue">;
def : CommandTimer<"thread exception">;
def : CommandTimer<"thread info">;
def : CommandTimer<"thread jump">;
def : CommandTimer<"thread list">;
def : CommandTimer<"thread plan discard">;
def : CommandTimer<"thread plan list">;
def : CommandTimer<"thread plan prune">;
def : CommandTimer<"thread return">;
def : CommandTimer<"thread select">;
def : CommandTimer<"thread siginfo">;
def : CommandTimer<"thread step-in">;
def : CommandTimer<"thread step-inst">;
def : CommandTimer<"thread step-inst-over">;
def : CommandTimer<"thread step-out">;
def : CommandTimer<"thread step-over">;
def : CommandTimer<"thread step-scripted">;
def : CommandTimer<"thread trace dump function-calls">;
def : CommandTimer<"thread trace dump info">;
def : CommandTimer<"thread trace dump instructions">;
def : CommandTimer<"thread trace export ctf">;
def : CommandTimer<"thread trace start">;
def : CommandTimer<"thread trace stop">;
def : CommandTimer<"thread until">;
def : CommandTimer<"trace dump">;
def : CommandTimer<"trace load">;
def : CommandTimer<"trace save">;
def : CommandTimer<"trace schema">;
def : CommandTimer<"type category define">;
def : CommandTimer<"type category delete">;
def : CommandTimer<"type category disable">;
def : CommandTimer<"type category enable">;
def : CommandTimer<"type category list">;
def : CommandTimer<"type filter add">;
def : CommandTimer<"type filter clear">;
def : CommandTimer<"type filter delete">;
def : CommandTimer<"type filter list">;
def : CommandTimer<"type format add">;
def : CommandTimer<"type format clear">;
def : CommandTimer<"type format delete">;
def : CommandTimer<"type format info">;
def : CommandTimer<"type format list">;
def : CommandTimer<"type lookup">;
def : CommandTimer<"type summary add">;
def : CommandTimer<"type summary clear">;
def : CommandTimer<"type summary delete">;
def : CommandTimer<"type summary info">;
def : CommandTimer<"type summary list">;
def : CommandTimer<"type synthetic add">;
def : CommandTimer<"type synthetic clear">;
def : CommandTimer<"type synthetic delete">;
def : CommandTimer<"type synthetic info">;
def : CommandTimer<"type synthetic list">;
def : CommandTimer<"version">;
def : CommandTimer<"watchpoint command add">;
def : CommandTimer<"watchpoint command delete">;
def : CommandTimer<"watchpoint command list">;
def : CommandTimer<"watchpoint delete">;
def : CommandTimer<"watchpoint disable">;
def : CommandTimer<"watchpoint enable">;
def : CommandTimer<"watchpoint ignore">;
def : CommandTimer<"watchpoint list">;
def : CommandTimer<"watchpoint modify">;
def : CommandTimer<"watchpoint set expression">;
def : CommandTimer<"watchpoint set variable">;

// Used by all commands that aren't listed above, e.g., user-defined commands.
def : TimerCategory<"Command.Other">;

// Only used by unittests/Utility/TimerTest.cpp.
def : TimerCategory<"TimerTest.A">;
def : TimerCategory<"TimerTest.B">;
