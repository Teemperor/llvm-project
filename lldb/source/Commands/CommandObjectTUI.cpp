//===-- CommandObjectTUI.cpp ----------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CommandObjectTUI.h"

#include "lldb/Core/Debugger.h"
#include "lldb/Core/IOHandler.h"
#include "lldb/Host/Pipe.h"
#include "lldb/Interpreter/CommandInterpreter.h"
#include "lldb/Interpreter/CommandReturnObject.h"
#include "lldb/Interpreter/ScriptInterpreter.h"
#include "lldb/Utility/Listener.h"
#include "lldb/Utility/Status.h"
#include "llvm/Support/Error.h"

#include <atomic>
#include <mutex>
#include <thread>

using namespace lldb;
using namespace lldb_private;

/// The Python function that runs the interactive interface. It is called like
/// a regular scripted command with the read end of the output pipe as its
/// argument.
static const char *g_tui_main_function = "lldb.tui.app.tui_main";
/// The Python function that handles 'tui <subcommand>' (e.g. 'tui set').
static const char *g_tui_command_function = "lldb.tui.app.tui_command";

static bool ImportTUIModule(ScriptInterpreter &interpreter,
                            CommandReturnObject &result) {
  CommandReturnObject import_result(/*colors=*/false);
  ExecuteScriptOptions options;
  options.SetEnableIO(false);
  if (interpreter.ExecuteOneLine("import lldb.tui.app", &import_result,
                                 options))
    return true;
  result.AppendErrorWithFormatv("unable to load the lldb.tui Python module: {0}",
                                import_result.GetErrorString());
  return false;
}

namespace {
/// The IOHandler that is on top of the IOHandler stack while the interface is
/// shown. Everything that would normally be printed asynchronously to the
/// terminal (process state changes, process output, output of scripts, ...) is
/// written into a pipe that the Python side reads from and displays.
class IOHandlerPythonTUI : public IOHandler {
public:
  IOHandlerPythonTUI(Debugger &debugger, ScriptInterpreter &interpreter)
      : IOHandler(debugger, IOHandler::Type::Curses),
        m_interpreter(interpreter) {}

  ~IOHandlerPythonTUI() override { ClosePipe(); }

  void Run() override {
    CommandReturnObject result(/*colors=*/false);
    result.SetInteractive(true);

    Status pipe_status = m_pipe.CreateNew();
    if (pipe_status.Fail()) {
      result.AppendErrorWithFormatv("unable to create output pipe: {0}",
                                    pipe_status.AsCString());
    } else {
      // Forward process events to a listener instead of letting the debugger
      // handle them. Otherwise the debugger prints stop notifications and
      // process output to the terminal and pushes the IOHandler of the process
      // on top of this one whenever the process is resumed. This is what the
      // built-in curses GUI does as well. The interface polls the state of the
      // process and reads its output itself.
      ListenerSP listener_sp = Listener::MakeListener("lldb.tui.events");
      m_debugger.EnableForwardEvents(listener_sp);
      // The interface doesn't need the events, but they have to be removed
      // from the listener so they don't pile up.
      std::atomic<bool> stop_draining(false);
      std::thread drain_thread([&]() {
        EventSP event_sp;
        while (!stop_draining)
          listener_sp->GetEvent(event_sp, std::chrono::milliseconds(100));
      });

      std::string args = std::to_string(m_pipe.GetReadFileDescriptor());
      Status error;
      ExecutionContext exe_ctx =
          m_debugger.GetCommandInterpreter().GetExecutionContext();
      if (!m_interpreter.RunScriptBasedCommand(
              g_tui_main_function, args, eScriptedCommandSynchronicityCurrentValue,
              result, error, exe_ctx) &&
          error.Fail())
        result.AppendError(error.AsCString());

      m_debugger.CancelForwardEvents(listener_sp);
      stop_draining = true;
      drain_thread.join();
    }
    ClosePipe();

    // Report whatever the interface left behind (e.g. an error that prevented
    // it from starting) on the regular terminal.
    std::string out = result.GetOutputString().str();
    std::string err = result.GetErrorString();
    if (!out.empty())
      m_debugger.PrintAsync(out.data(), out.size(), /*is_stdout=*/true);
    if (!err.empty())
      m_debugger.PrintAsync(err.data(), err.size(), /*is_stdout=*/false);
    SetIsDone(true);
  }

  void Cancel() override {}

  bool Interrupt() override {
    return m_debugger.GetCommandInterpreter().IOHandlerInterrupt(*this);
  }

  void GotEOF() override {}

  void PrintAsync(const char *s, size_t len, bool is_stdout) override {
    std::lock_guard<std::mutex> guard(m_pipe_mutex);
    if (!m_pipe.CanWrite())
      return;
    // Never block the thread that is printing for long. If the interface
    // doesn't drain the pipe in time, the output is dropped.
    while (len > 0) {
      llvm::Expected<size_t> written =
          m_pipe.Write(s, len, std::chrono::milliseconds(100));
      if (!written) {
        llvm::consumeError(written.takeError());
        return;
      }
      if (*written == 0)
        return;
      s += *written;
      len -= *written;
    }
  }

private:
  void ClosePipe() {
    std::lock_guard<std::mutex> guard(m_pipe_mutex);
    m_pipe.Close();
  }

  ScriptInterpreter &m_interpreter;
  std::mutex m_pipe_mutex;
  Pipe m_pipe;
};
} // namespace

CommandObjectTUI::CommandObjectTUI(CommandInterpreter &interpreter)
    : CommandObjectRaw(
          interpreter, "tui",
          "Switch into the Python and curses based text user interface. "
          "Run 'tui help' for the list of subcommands that configure the "
          "interface (e.g. from an .lldbinit file).",
          "tui [<subcommand> [<args>...]]") {}

CommandObjectTUI::~CommandObjectTUI() = default;

void CommandObjectTUI::DoExecute(llvm::StringRef command,
                                 CommandReturnObject &result) {
  Debugger &debugger = GetDebugger();
  ScriptInterpreter *interpreter =
      debugger.GetScriptInterpreter(/*can_create=*/true, eScriptLanguagePython);
  if (!interpreter) {
    result.AppendError("the tui command requires LLDB to be built with Python "
                       "support.");
    return;
  }
  if (!ImportTUIModule(*interpreter, result))
    return;

  command = command.trim();
  if (!command.empty()) {
    // Configuration subcommands are handled synchronously by Python.
    Status error;
    ExecutionContext exe_ctx = m_exe_ctx;
    if (!interpreter->RunScriptBasedCommand(
            g_tui_command_function, command,
            eScriptedCommandSynchronicitySynchronous, result, error, exe_ctx) &&
        error.Fail())
      result.AppendError(error.AsCString());
    else if (result.GetStatus() == eReturnStatusInvalid)
      result.SetStatus(eReturnStatusSuccessFinishResult);
    return;
  }

  FileSP input_sp = debugger.GetInputFileSP();
  FileSP output_sp = debugger.GetOutputFileSP();
  if (!input_sp || !output_sp || !input_sp->GetIsRealTerminal() ||
      !input_sp->GetIsInteractive() || !output_sp->GetIsRealTerminal()) {
    result.AppendError("the tui command requires an interactive terminal.");
    return;
  }

  debugger.RunIOHandlerAsync(
      std::make_shared<IOHandlerPythonTUI>(debugger, *interpreter));
  result.SetStatus(eReturnStatusSuccessFinishNoResult);
}
