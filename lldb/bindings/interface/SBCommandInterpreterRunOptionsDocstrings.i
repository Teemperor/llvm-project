%feature("docstring",
"SBCommandInterpreterRunOptions controls how the RunCommandInterpreter runs the code it is fed.

A default SBCommandInterpreterRunOptions object has:

* StopOnContinue: false
* StopOnError:    false
* StopOnCrash:    false
* EchoCommands:   true
* PrintResults:   true
* PrintErrors:    true
* AddToHistory:   true
* AllowRepeats    false

Interactive debug sessions always allow repeats, the AllowRepeats
run option only affects non-interactive sessions.

For example, running the commands of a file without stopping at the first
error::

    options = lldb.SBCommandInterpreterRunOptions()
    options.SetStopOnError(False)
    options.SetPrintResults(True)
    result = lldb.SBCommandReturnObject()
    interpreter.HandleCommandsFromFile(lldb.SBFileSpec('commands.txt'), options, result)

See also `SBDebugger.RunCommandInterpreter` and
`SBCommandInterpreter.HandleCommandsFromFile`.
") lldb::SBCommandInterpreterRunOptions;

%feature("docstring",
"Returns whether running commands stops when one of them resumes the process."
) lldb::SBCommandInterpreterRunOptions::GetStopOnContinue;

%feature("docstring",
"Sets whether running commands stops when one of them resumes the process.

Useful when a list of commands should not keep running after the process was
continued."
) lldb::SBCommandInterpreterRunOptions::SetStopOnContinue;

%feature("docstring",
"Returns whether running commands stops at the first error."
) lldb::SBCommandInterpreterRunOptions::GetStopOnError;

%feature("docstring",
"Sets whether running commands stops at the first error."
) lldb::SBCommandInterpreterRunOptions::SetStopOnError;

%feature("docstring",
"Returns whether running commands stops when the process crashes."
) lldb::SBCommandInterpreterRunOptions::GetStopOnCrash;

%feature("docstring",
"Sets whether running commands stops when the process crashes."
) lldb::SBCommandInterpreterRunOptions::SetStopOnCrash;

%feature("docstring",
"Returns whether the commands themselves are printed as they are run."
) lldb::SBCommandInterpreterRunOptions::GetEchoCommands;

%feature("docstring",
"Sets whether the commands themselves are printed as they are run."
) lldb::SBCommandInterpreterRunOptions::SetEchoCommands;

%feature("docstring",
"Returns whether comment lines are printed as they are read."
) lldb::SBCommandInterpreterRunOptions::GetEchoCommentCommands;

%feature("docstring",
"Sets whether comment lines are printed as they are read.

Only has an effect if echoing commands is enabled, see
`SBCommandInterpreterRunOptions.SetEchoCommands`."
) lldb::SBCommandInterpreterRunOptions::SetEchoCommentCommands;

%feature("docstring",
"Returns whether the output of the commands is printed."
) lldb::SBCommandInterpreterRunOptions::GetPrintResults;

%feature("docstring",
"Sets whether the output of the commands is printed.

If this is ``False`` the output is only available through the
`SBCommandReturnObject` of the commands."
) lldb::SBCommandInterpreterRunOptions::SetPrintResults;

%feature("docstring",
"Returns whether the errors of the commands are printed."
) lldb::SBCommandInterpreterRunOptions::GetPrintErrors;

%feature("docstring",
"Sets whether the errors of the commands are printed."
) lldb::SBCommandInterpreterRunOptions::SetPrintErrors;

%feature("docstring",
"Returns whether the commands are added to the command history."
) lldb::SBCommandInterpreterRunOptions::GetAddToHistory;

%feature("docstring",
"Sets whether the commands are added to the command history.

Commands that a script runs are usually better kept out of the history the user
navigates with the up arrow key."
) lldb::SBCommandInterpreterRunOptions::SetAddToHistory;

%feature("docstring",
"Returns whether the process events are handled while commands run."
) lldb::SBCommandInterpreterRunOptions::GetAutoHandleEvents;

%feature("docstring",
"Sets whether the process events are handled while commands run.

With this enabled the interpreter behaves like the command line interface: it
prints the process output and reports where the process stopped."
) lldb::SBCommandInterpreterRunOptions::SetAutoHandleEvents;

%feature("docstring",
"Returns whether a separate thread handles the input and output."
) lldb::SBCommandInterpreterRunOptions::GetSpawnThread;

%feature("docstring",
"Sets whether a separate thread handles the input and output."
) lldb::SBCommandInterpreterRunOptions::SetSpawnThread;

%feature("docstring",
"Returns whether an empty line repeats the previous command."
) lldb::SBCommandInterpreterRunOptions::GetAllowRepeats;

%feature("docstring",
"Sets whether an empty line repeats the previous command.

By default, `SBDebugger.RunCommandInterpreter` will discard repeats if the
IOHandler being used is not interactive.  Setting AllowRepeats to true
will override this behavior and always process empty lines in the input
as a repeat command."
) lldb::SBCommandInterpreterRunOptions::SetAllowRepeats;

%feature("docstring",
"The result of running the command interpreter.

Returned by the overload of `SBDebugger.RunCommandInterpreter` that takes only
an `SBCommandInterpreterRunOptions`. It says how many errors occurred and why the
interpreter stopped."
) lldb::SBCommandInterpreterRunResult;

%feature("docstring",
"Returns how many errors occurred while the interpreter was running."
) lldb::SBCommandInterpreterRunResult::GetNumberOfErrors;

%feature("docstring",
"Returns why the interpreter stopped.

The result is one of the ``lldb.eCommandInterpreterResult*`` enumerators, which
distinguishes for example a clean quit from a crash of the debugged process."
) lldb::SBCommandInterpreterRunResult::GetResult;
