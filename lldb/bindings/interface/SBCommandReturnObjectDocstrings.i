%feature("docstring",
"Represents a container which holds the result from command execution.
It works with :py:class:`SBCommandInterpreter.HandleCommand()` to encapsulate the result
of command execution.

A return object holds the output, the errors and the status of a command that was
run with `SBCommandInterpreter.HandleCommand`::

    result = lldb.SBCommandReturnObject()
    interpreter.HandleCommand(\'breakpoint list\', result)
    if result.Succeeded():
        print(result.GetOutput())
    else:
        print(result.GetError())

Scripted commands receive one as their ``result`` argument and use
`SBCommandReturnObject.AppendMessage` and
`SBCommandReturnObject.SetError` to report back to the user, see
`SBCommandInterpreter.AddCommand`.

See :py:class:`SBCommandInterpreter` for example usage of SBCommandReturnObject."
) lldb::SBCommandReturnObject;

%feature("docstring",
"Returns whether this object holds a result."
) lldb::SBCommandReturnObject::IsValid;

%feature("docstring",
"Returns the command line of the command this result belongs to."
) lldb::SBCommandReturnObject::GetCommand;

%feature("docstring",
"Returns everything the command wrote to its standard output.

Returns an empty string if the command produced no output. In Python ``str()`` of
a return object also includes the output."
) lldb::SBCommandReturnObject::GetOutput;

%feature("docstring",
"Returns everything the command wrote to its error output.

Returns an empty string if the command reported no errors, see
`SBCommandReturnObject.Succeeded`."
) lldb::SBCommandReturnObject::GetError;

%feature("docstring",
"Returns the error of the command as `SBStructuredData`.

Structured errors carry machine readable details in addition to the message."
) lldb::SBCommandReturnObject::GetErrorData;

%feature("docstring",
"Returns the number of characters of the output, see
`SBCommandReturnObject.GetOutput`."
) lldb::SBCommandReturnObject::GetOutputSize;

%feature("docstring",
"Returns the number of characters of the error output, see
`SBCommandReturnObject.GetError`."
) lldb::SBCommandReturnObject::GetErrorSize;

%feature("docstring",
"Writes the output of the command to the given file.

In Python a file object can be passed directly. Returns the number of characters
that were written."
) lldb::SBCommandReturnObject::PutOutput;

%feature("docstring",
"Writes the error output of the command to the given file.

See `SBCommandReturnObject.PutOutput`."
) lldb::SBCommandReturnObject::PutError;

%feature("docstring",
"Resets this object, discarding the output, the errors and the status."
) lldb::SBCommandReturnObject::Clear;

%feature("docstring",
"Returns the status of the command as one of the ``lldb.eReturnStatus*``
enumerators.

See `SBCommandReturnObject.Succeeded` for a simple success check."
) lldb::SBCommandReturnObject::GetStatus;

%feature("docstring",
"Sets the status of the command.

``status`` is one of the ``lldb.eReturnStatus*`` enumerators. Scripted commands
use this to report how they finished."
) lldb::SBCommandReturnObject::SetStatus;

%feature("docstring",
"Returns whether the command succeeded."
) lldb::SBCommandReturnObject::Succeeded;

%feature("docstring",
"Returns whether the command produced any output."
) lldb::SBCommandReturnObject::HasResult;

%feature("docstring",
"Appends a line to the output of the command.

This is how a scripted command prints something::

    def __call__(self, debugger, command, exe_ctx, result):
        result.AppendMessage(\'hello\')
"
) lldb::SBCommandReturnObject::AppendMessage;

%feature("docstring",
"Appends a warning to the output of the command."
) lldb::SBCommandReturnObject::AppendWarning;

%feature("docstring",
"Writes a description of this result into the given `SBStream`."
) lldb::SBCommandReturnObject::GetDescription;

%feature("docstring",
"Makes the output of the command go to the given file as it is produced.

In Python a file object can be passed directly. Without an immediate output file
the output is collected and can be read with
`SBCommandReturnObject.GetOutput` after the command finished."
) lldb::SBCommandReturnObject::SetImmediateOutputFile;

%feature("docstring",
"Makes the error output of the command go to the given file as it is produced.

See `SBCommandReturnObject.SetImmediateOutputFile`."
) lldb::SBCommandReturnObject::SetImmediateErrorFile;

%feature("docstring",
"Appends the given string to the output of the command.

Unlike `SBCommandReturnObject.AppendMessage` this does not add a newline."
) lldb::SBCommandReturnObject::PutCString;

%feature("docstring",
"Reports that the command failed with the given error.

Takes either an error message as a string or an `SBError`, and optionally a
fallback message that is used if the error has no message. This also sets the
status of the result to a failure::

    def __call__(self, debugger, command, exe_ctx, result):
        if not exe_ctx.GetProcess():
            result.SetError(\'this command needs a running process\')
            return
"
) lldb::SBCommandReturnObject::SetError;

%feature("docstring",
"Returns the values the command produced as an `SBValueList`.

Some commands, such as ``expression``, report their results as values in addition
to the textual output."
) lldb::SBCommandReturnObject::GetValues;
