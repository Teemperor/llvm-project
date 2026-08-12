%feature("docstring",
"Describes the program context in which a command should be executed.

An execution context bundles a target, a process, a thread and a frame; the ones
that are set say what \"the current\" target, thread and frame are for whatever
uses the context.

Scripted commands receive an execution context as their ``exe_ctx`` argument so
they know what the user was looking at when the command was run
(`SBCommandInterpreter.AddCommand`), and
`SBCommandInterpreter.HandleCommand` takes one to run a command in a specific
context::

    class MyCommand:
        def __call__(self, debugger, command, exe_ctx, result):
            frame = exe_ctx.GetFrame()
            result.AppendMessage(frame.GetFunctionName())

An execution context can also be constructed from an `SBTarget`, `SBProcess`,
`SBThread` or `SBFrame`, in which case the other members are filled in from
it."
) lldb::SBExecutionContext;

%feature("docstring",
"Returns the `SBTarget` of this context."
) lldb::SBExecutionContext::GetTarget;

%feature("docstring",
"Returns the `SBProcess` of this context.

The result may be invalid if the context has no process."
) lldb::SBExecutionContext::GetProcess;

%feature("docstring",
"Returns the `SBThread` of this context.

The result may be invalid if the context has no thread."
) lldb::SBExecutionContext::GetThread;

%feature("docstring",
"Returns the `SBFrame` of this context.

The result may be invalid if the context has no frame."
) lldb::SBExecutionContext::GetFrame;
