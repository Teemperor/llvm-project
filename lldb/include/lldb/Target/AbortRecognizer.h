//===-- AbortRecognizer.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef liblldb_AbortRegognizer_h_
#define liblldb_AbortRegognizer_h_

#include "lldb/Target/Process.h"
#include "lldb/Target/StackFrameRecognizer.h"
#include "lldb/Utility/ConstString.h"
#include "lldb/Utility/FileSpec.h"

#include <tuple>

namespace lldb_private {

void RegisterAbortRecognizer(Process *process);

class AbortRecognizerHandler {
public:
  ~AbortRecognizerHandler() = default;

  static std::tuple<FileSpec, ConstString>
  GetAbortLocation(Process *process_sp);
  static std::tuple<FileSpec, ConstString>
  GetAssertLocation(Process *process_sp);
};

#pragma mark Frame recognizers

class AbortRecognizedStackFrame : public RecognizedStackFrame {
public:
  AbortRecognizedStackFrame(lldb::StackFrameSP frame_sp);
  lldb::StackFrameSP GetMostRelevantFrame() override;
  lldb::StopInfoSP GetStopInfo() override;

private:
  lldb::StackFrameSP m_most_relevant_frame;
  lldb::StopInfoSP m_stop_info_sp;
};

class AbortFrameRecognizer : public StackFrameRecognizer {
public:
  std::string GetName() override { return "Abort StackFrame Recognizer"; }
  lldb::RecognizedStackFrameSP
  RecognizeFrame(lldb::StackFrameSP frame) override;
};

} // namespace lldb_private

#endif // liblldb_AbortRecognizer_h_
