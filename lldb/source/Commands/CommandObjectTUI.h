//===-- CommandObjectTUI.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLDB_SOURCE_COMMANDS_COMMANDOBJECTTUI_H
#define LLDB_SOURCE_COMMANDS_COMMANDOBJECTTUI_H

#include "lldb/Interpreter/CommandObject.h"

namespace lldb_private {

/// The 'tui' command. The text user interface itself is implemented in Python
/// (the lldb.tui package that is distributed with LLDB's Python module). This
/// command only forwards to it and provides the IOHandler that the interface
/// runs in, so that asynchronous debugger output is routed into the interface
/// instead of being written over the curses screen.
class CommandObjectTUI : public CommandObjectRaw {
public:
  CommandObjectTUI(CommandInterpreter &interpreter);

  ~CommandObjectTUI() override;

protected:
  void DoExecute(llvm::StringRef command, CommandReturnObject &result) override;
};

} // namespace lldb_private

#endif // LLDB_SOURCE_COMMANDS_COMMANDOBJECTTUI_H
