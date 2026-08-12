%feature("docstring",
"Represents a processor trace of a process.

A trace is a recording of everything a program executed, produced by a tracing
technology such as Intel Processor Trace. Traces are created for a live process
with `SBTarget.CreateTrace` and started with `SBTrace.Start`, or loaded from a
trace bundle on disk with `SBDebugger.LoadTraceFromFile`.

Once a trace exists, `SBTrace.CreateNewCursor` hands out an `SBTraceCursor` that
walks the instructions a thread executed::

    error = lldb.SBError()
    trace = target.CreateTrace(error)
    trace.Start(lldb.SBStructuredData())
    process.Continue()

    cursor = trace.CreateNewCursor(error, thread)
    while cursor.HasValue():
        if cursor.IsInstruction():
            print(hex(cursor.GetLoadAddress()))
        cursor.Next()

This is the API behind the ``thread trace`` and ``process trace`` commands; see
:doc:`/use/intel_pt` for what tracing requires and how to configure it."
) lldb::SBTrace;

%feature("docstring",
"Returns whether this object refers to a trace."
) lldb::SBTrace::IsValid;

%feature("docstring",
"Loads a trace from a trace bundle description file.

This is a class method; see `SBDebugger.LoadTraceFromFile`, which is the more
convenient way to use it."
) lldb::SBTrace::LoadTraceFromFile;

%feature("docstring",
"Returns an `SBTraceCursor` for the trace of the given thread.

If the thread is not traced or its trace information failed to load, an invalid
cursor is returned and ``error`` is set. The cursor initially points at the end
of the trace and moves backwards by default, see
`SBTraceCursor.SetForwards`."
) lldb::SBTrace::CreateNewCursor;

%feature("docstring",
"Saves this trace to a directory, which is created if needed.

This also creates a ``trace.json`` file with the main properties of the trace
session along with the files that hold the actual trace data. That file can be
used later as the input of the ``trace load`` command or of
`SBDebugger.LoadTraceFromFile`.

:param error: Set with an error in case of failures.
:param bundle_dir: The directory where the trace files will be saved.
:param compact: Try not to save information that is irrelevant to the traced
    processes. Each trace plug-in implements this in a different fashion.
:return: An `SBFileSpec` pointing to the bundle description file."
) lldb::SBTrace::SaveToDisk;

%feature("docstring",
"Returns a description of the parameters `SBTrace.Start` accepts.

Each tracing technology has its own configuration options; this returns their
documentation as a string, or ``None`` if this object is invalid."
) lldb::SBTrace::GetStartConfigurationHelp;

%feature("docstring",
"Starts tracing.

The overload that only takes a configuration traces all current and future
threads of the process, which is what the ``process trace start`` command does.
The overload that also takes an `SBThread` traces only that thread, like
``thread trace start``.

``configuration`` is an `SBStructuredData` dictionary with the options of the
tracing technology; `SBTrace.GetStartConfigurationHelp` describes which options
exist. Returns an `SBError` explaining any failure, for example when tracing was
already started."
) lldb::SBTrace::Start;

%feature("docstring",
"Stops tracing.

The overload without arguments stops tracing the whole process, the one that
takes an `SBThread` stops tracing that thread. Returns an `SBError` explaining
any failure."
) lldb::SBTrace::Stop;
