//===-- AbortRecognizer.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "lldb/Core/Module.h"
#include "lldb/Symbol/Function.h"
#include "lldb/Symbol/SymbolContext.h"
#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrameList.h"
#include "lldb/Target/Target.h"
#include "lldb/Target/Thread.h"

#include "lldb/Utility/Log.h"
#include "lldb/Utility/Logging.h"

#include "lldb/Target/AbortRecognizer.h"

using namespace lldb;
using namespace lldb_private;

std::tuple<FileSpec, ConstString>
AbortRecognizerHandler::GetAbortLocation(Process *process) {
  Target &target = process->GetTarget();

  std::string module_name;
  std::string symbol_name;

  switch (target.GetArchitecture().GetTriple().getOS()) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    module_name = "libsystem_kernel.dylib";
    symbol_name = "__pthread_kill";
    break;
  case llvm::Triple::Linux:
    module_name = "libc.so.6";
    symbol_name = "__GI_raise";
    break;
  default:
    llvm_unreachable("Abort Recognizer: Unsupported OS");
  }

  return std::make_tuple(FileSpec(module_name), ConstString(symbol_name));
}

std::tuple<FileSpec, ConstString>
AbortRecognizerHandler::GetAssertLocation(Process *process) {
  Target &target = process->GetTarget();

  std::string module_name;
  std::string symbol_name;

  switch (target.GetArchitecture().GetTriple().getOS()) {
  case llvm::Triple::Darwin:
  case llvm::Triple::MacOSX:
    module_name = "libsystem_c.dylib";
    symbol_name = "__assert_rtn";
    break;
  case llvm::Triple::Linux:
    module_name = "libc.so.6";
    symbol_name = "__GI___assert_fail";
    break;
  default:
    llvm_unreachable("Abort Recognizer: Unsupported OS");
  }

  return std::make_tuple(FileSpec(module_name), ConstString(symbol_name));
}

#pragma mark Frame recognizers

AbortRecognizedStackFrame::AbortRecognizedStackFrame(
    StackFrameSP cur_frame_sp) {
  if (cur_frame_sp->GetFrameIndex())
    return;

  ThreadSP thread_sp = cur_frame_sp->GetThread();
  ProcessSP process_sp = thread_sp->GetProcess();

  const uint32_t frames_to_fetch = 10;
  StackFrameSP prev_frame_sp = nullptr;

  FileSpec module_spec;
  ConstString symbol_name;
  std::tie(module_spec, symbol_name) =
      AbortRecognizerHandler::GetAssertLocation(process_sp.get());

  // MARK: Fetch most relevant frame
  for (uint32_t i = 0; i < frames_to_fetch; i++) {
    prev_frame_sp = thread_sp->GetStackFrameAtIndex(i);

    if (!prev_frame_sp) {
      Log *log(lldb_private::GetLogIfAllCategoriesSet(LIBLLDB_LOG_UNWIND));
      LLDB_LOG(log, "Abort Recognizer: Hit unwinding bound ({1} frames)!",
               frames_to_fetch);
      break;
    }

    SymbolContext sym_ctx =
        prev_frame_sp->GetSymbolContext(eSymbolContextEverything);

    if (sym_ctx.module_sp->GetFileSpec().GetFilename() ==
            module_spec.GetFilename() &&
        sym_ctx.GetFunctionName() == symbol_name) {
      if (i < frames_to_fetch - 1)
        m_most_relevant_frame = thread_sp->GetStackFrameAtIndex(i + 1);
      else
        m_most_relevant_frame = thread_sp->GetStackFrameAtIndex(i);
      break;
    }
  }

  m_stop_info_sp = StopInfo::CreateStopReasonForRecognizedFrame(
      *thread_sp.get(), "hit program assert");
}

lldb::RecognizedStackFrameSP
AbortFrameRecognizer::RecognizeFrame(lldb::StackFrameSP frame) {
  return lldb::RecognizedStackFrameSP(new AbortRecognizedStackFrame(frame));
};

lldb::StackFrameSP AbortRecognizedStackFrame::GetMostRelevantFrame() {
  return m_most_relevant_frame;
}

lldb::StopInfoSP AbortRecognizedStackFrame::GetStopInfo() {
  return m_stop_info_sp;
}

namespace lldb_private {

void RegisterAbortRecognizer(Process *process) {
  static llvm::once_flag g_once_flag;
  llvm::call_once(g_once_flag, [process]() {
    FileSpec module;
    ConstString function;
    std::tie(module, function) =
        AbortRecognizerHandler::GetAbortLocation(process);
    StackFrameRecognizerManager::AddRecognizer(
        StackFrameRecognizerSP(new AbortFrameRecognizer()),
        module.GetFilename(), function, /*first_instruction_only*/ false);
  });
}

} // namespace lldb_private
