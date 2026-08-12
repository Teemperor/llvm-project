%feature("docstring",
"SBDebugger is the primordial object that creates SBTargets and provides
access to them.  It also manages the overall debugging experiences.

A debugger owns a list of targets (`SBDebugger.CreateTarget`,
`SBDebugger.GetTargetAtIndex`), the command interpreter
(`SBDebugger.GetCommandInterpreter`), the settings of the debug session and the
data formatters (`SBDebugger.GetCategory`). Almost every script starts by
creating one::

    import lldb
    lldb.SBDebugger.Initialize()
    debugger = lldb.SBDebugger.Create()
    ...
    lldb.SBDebugger.Destroy(debugger)
    lldb.SBDebugger.Terminate()

Inside the ``lldb`` command line tool and in scripts that are run by it, the
current debugger is already available as ``lldb.debugger``, so it neither has to
be created nor initialized.

`SBDebugger.SetAsync` decides whether execution control functions such as
`SBProcess.Continue` return immediately (asynchronous, the default) or only
once the process stopped again (synchronous). Asynchronous mode requires
handling process events, see `SBListener`.

For example (from example/disasm.py),::

    import lldb
    import os
    import sys

    def disassemble_instructions(insts):
        for i in insts:
            print(i)

    ...

    # Create a new debugger instance
    debugger = lldb.SBDebugger.Create()

    # When we step or continue, don't return from the function until the process
    # stops. We do this by setting the async mode to false.
    debugger.SetAsync(False)

    # Create a target from a file and arch
    print('Creating a target for \'%s\'' % exe)

    target = debugger.CreateTargetWithFileAndArch(exe, lldb.LLDB_ARCH_DEFAULT)

    if target:
        # If the target is valid set a breakpoint at main
        main_bp = target.BreakpointCreateByName(fname, target.GetExecutable().GetFilename())

        print(main_bp)

        # Launch the process. Since we specified synchronous mode, we won't return
        # from this function until we hit the breakpoint at main
        process = target.LaunchSimple(None, None, os.getcwd())

        # Make sure the launch went ok
        if process:
            # Print some simple process info
            state = process.GetState()
            print(process)
            if state == lldb.eStateStopped:
                # Get the first thread
                thread = process.GetThreadAtIndex(0)
                if thread:
                    # Print some simple thread info
                    print(thread)
                    # Get the first frame
                    frame = thread.GetFrameAtIndex(0)
                    if frame:
                        # Print some simple frame info
                        print(frame)
                        function = frame.GetFunction()
                        # See if we have debug info (a function)
                        if function:
                            # We do have a function, print some info for the function
                            print(function)
                            # Now get all instructions for this function and print them
                            insts = function.GetInstructions(target)
                            disassemble_instructions(insts)
                        else:
                            # See if we have a symbol in the symbol table for where we stopped
                            symbol = frame.GetSymbol()
                            if symbol:
                                # We do have a symbol, print some info for the symbol
                                print(symbol)
                                # Now get all instructions for this symbol and print them
                                insts = symbol.GetInstructions(target)
                                disassemble_instructions(insts)

                        registerList = frame.GetRegisters()
                        print('Frame registers (size of register set = %d):' % registerList.GetSize())
                        for value in registerList:
                            print('%s (number of children = %d):' % (value.GetName(), value.GetNumChildren()))
                            for child in value:
                                print('Name: ', child.GetName(), ' Value: ', child.GetValue())

                print('Hit the breakpoint at main, enter to continue and wait for program to exit or \'Ctrl-D\'/\'quit\' to terminate the program')
                next = sys.stdin.readline()
                if not next or next.rstrip('\\n') == 'quit':
                    print('Terminating the inferior process...')
                    process.Kill()
                else:
                    # Now continue to the program exit
                    process.Continue()
                    # When we return from the above function we will hopefully be at the
                    # program exit. Print out some process info
                    print(process)
            elif state == lldb.eStateExited:
                print('Didn\'t hit the breakpoint at main, program has exited...')
            else:
                print('Unexpected process state: %s, killing process...' % debugger.StateAsCString(state))
                process.Kill()

Sometimes you need to create an empty target that will get filled in later.  The most common use for this
is to attach to a process by name or pid where you don't know the executable up front.  The most convenient way
to do this is: ::

    target = debugger.CreateTarget('')
    error = lldb.SBError()
    process = target.AttachToProcessWithName(debugger.GetListener(), 'PROCESS_NAME', False, error)

or the equivalent arguments for :py:class:`SBTarget.AttachToProcessWithID` .

In Python an SBDebugger is iterable and yields its targets, and ``len()``
returns their number."
) lldb::SBDebugger;

%feature("docstring",
    "Returns the name of the broadcaster class that sends debugger events
    (``lldb.debugger``).

    Debugger events include progress reports and diagnostics, see
    `SBDebugger.GetProgressFromEvent` and
    `SBDebugger.GetDiagnosticFromEvent`."
) lldb::SBDebugger::GetBroadcasterClass;

%feature("docstring",
    "Returns whether the given language is supported by this build of LLDB.

    ``language`` is one of the ``lldb.eLanguageType*`` enumerators."
) lldb::SBDebugger::SupportsLanguage;

%feature("docstring",
    "Returns the `SBBroadcaster` of this debugger.

    Add it to an `SBListener` to receive the debugger's events, such as progress
    reports::

        listener = lldb.SBListener('my listener')
        debugger.GetBroadcaster().AddListener(listener,
                                             lldb.SBDebugger.eBroadcastBitProgress)
"
) lldb::SBDebugger::GetBroadcaster;

%feature("docstring",
    "Extracts the progress information from a progress event.

    Returns the message of the progress report, or ``None`` if the event is not
    a progress event. In Python the output parameters of the C++ API are
    returned as a tuple together with the message::

        message, progress_id, completed, total, is_debugger_specific = \\
            lldb.SBDebugger.GetProgressFromEvent(event)

    ``completed`` is ``0`` for the event that starts a progress report and equal
    to ``total`` for the one that ends it. A ``total`` of
    ``lldb.UINT64_MAX`` means the total amount of work is unknown, so an
    indeterminate progress indicator should be shown.

    See `SBProgress` for reporting progress from a script."
) lldb::SBDebugger::GetProgressFromEvent;

%feature("docstring",
    "Returns the progress information of a progress event as
    `SBStructuredData`.

    This is a more extensible alternative to
    `SBDebugger.GetProgressFromEvent`."
) lldb::SBDebugger::GetProgressDataFromEvent;

%feature("docstring",
    "Returns the diagnostic information of a warning or error event as
    `SBStructuredData`.

    Diagnostics are the warnings and errors LLDB itself emits, for example about
    missing debug information. Listen for
    ``lldb.SBDebugger.eBroadcastBitWarning`` and
    ``lldb.SBDebugger.eBroadcastBitError`` events to receive them."
) lldb::SBDebugger::GetDiagnosticFromEvent;

%feature("docstring",
    "Initializes LLDB and its subsystems.

    This has to be called once before any other LLDB function is used, and it
    should be paired with a call to `SBDebugger.Terminate`. Scripts that run
    inside the ``lldb`` command line tool don't need to call this.

    See `SBDebugger.InitializeWithErrorHandling` for a variant that reports
    failures."
) lldb::SBDebugger::Initialize;

%feature("docstring",
    "Initializes LLDB and its subsystems, returning an `SBError`.

    Like `SBDebugger.Initialize`, but failures are reported instead of being
    ignored."
) lldb::SBDebugger::InitializeWithErrorHandling;

%feature("docstring",
    "Installs a signal handler that prints a stack trace when LLDB crashes."
) lldb::SBDebugger::PrintStackTraceOnError;

%feature("docstring",
    "Makes LLDB print its diagnostics when it detects an internal error."
) lldb::SBDebugger::PrintDiagnosticsOnError;

%feature("docstring",
    "Shuts down LLDB and its subsystems.

    Call this once at the end of a program that called
    `SBDebugger.Initialize`. Using any LLDB API afterwards is not supported."
) lldb::SBDebugger::Terminate;

%feature("docstring",
    "Creates a new debugger instance.

    If ``source_init_files`` is ``True``, the ``~/.lldbinit`` file and other
    initialization files are read, which is usually not what a script wants.
    Every debugger that is created should eventually be passed to
    `SBDebugger.Destroy`::

        debugger = lldb.SBDebugger.Create(False)
"
) lldb::SBDebugger::Create;

%feature("docstring",
    "Destroys a debugger instance that was created with `SBDebugger.Create`.

    This releases all targets, processes and modules of the debugger. Note that
    the process of a target is killed or detached from depending on the
    ``target.detach-on-error`` setting and how the process was created."
) lldb::SBDebugger::Destroy;

%feature("docstring",
    "Tells LLDB that the system is low on memory so it can free caches.

    LLDB caches a lot of data (parsed debug information, disassembly, ...), and
    this asks it to release what it can."
) lldb::SBDebugger::MemoryPressureDetected;

%feature("docstring",
    "Returns whether this object refers to a debugger."
) lldb::SBDebugger::IsValid;

%feature("docstring",
    "Closes all IO handlers of this debugger and resets it to its initial
    state."
) lldb::SBDebugger::Clear;

%feature("docstring",
    "Returns the value of one or all debugger settings as `SBStructuredData`.

    Pass the name of a setting to get just that setting, a prefix to get a
    subtree of settings or nothing to get all of them::

        settings = debugger.GetSetting('target.arg0')
        settings = debugger.GetSetting('target')
        settings = debugger.GetSetting()

    See `SBDebugger.SetInternalVariable` for changing a setting."
) lldb::SBDebugger::GetSetting;

%feature("docstring",
    "Sets whether execution control functions return immediately.

    In asynchronous mode (the default, ``True``) functions such as
    `SBProcess.Continue` and the stepping functions of `SBThread` return as soon
    as the process was resumed, and the caller has to wait for the process
    events to find out when it stopped again (see `SBListener`).

    In synchronous mode (``False``) those functions only return once the process
    stopped again, which is much simpler to use in scripts::

        debugger.SetAsync(False)
        process.Continue()   # Returns after the next stop.
"
) lldb::SBDebugger::SetAsync;

%feature("docstring",
    "Returns whether the debugger is in asynchronous mode, see
    `SBDebugger.SetAsync`."
) lldb::SBDebugger::GetAsync;

%feature("docstring",
    "Sets whether LLDB's initialization files (``~/.lldbinit``) are skipped."
) lldb::SBDebugger::SkipLLDBInitFiles;

%feature("docstring",
    "Sets whether application specific initialization files are skipped."
) lldb::SBDebugger::SkipAppInitFiles;

%feature("docstring",
    "Deprecated, use `SBDebugger.SetInputFile`."
) lldb::SBDebugger::SetInputFileHandle;

%feature("docstring",
    "Deprecated, use `SBDebugger.SetOutputFile`."
) lldb::SBDebugger::SetOutputFileHandle;

%feature("docstring",
    "Deprecated, use `SBDebugger.SetErrorFile`."
) lldb::SBDebugger::SetErrorFileHandle;

%feature("docstring",
    "Feeds the given string to the debugger as if it was typed on its input.

    Useful to drive the command interpreter from a script::

        debugger.SetInputString('breakpoint list\\nquit\\n')
        debugger.RunCommandInterpreter(True, False)
"
) lldb::SBDebugger::SetInputString;

%feature("docstring",
    "Sets the file the debugger reads commands from.

    Takes an `SBFile`, which can be created from a Python file object with
    `SBFile.Create`."
) lldb::SBDebugger::SetInputFile;

%feature("docstring",
    "Sets the file the debugger writes its output to.

    Takes an `SBFile`. This is where command results and process output are
    written::

        debugger.SetOutputFile(lldb.SBFile.Create(open('/tmp/log.txt', 'w')))
"
) lldb::SBDebugger::SetOutputFile;

%feature("docstring",
    "Sets the file the debugger writes error messages to.

    Takes an `SBFile`, see `SBDebugger.SetOutputFile`."
) lldb::SBDebugger::SetErrorFile;

%feature("docstring",
    "Returns the `SBFile` the debugger reads commands from."
) lldb::SBDebugger::GetInputFile;

%feature("docstring",
    "Returns the `SBFile` the debugger writes its output to."
) lldb::SBDebugger::GetOutputFile;

%feature("docstring",
    "Returns the `SBFile` the debugger writes error messages to."
) lldb::SBDebugger::GetErrorFile;

%feature("docstring",
    "Deprecated, use `SBDebugger.GetInputFile`."
) lldb::SBDebugger::GetInputFileHandle;

%feature("docstring",
    "Deprecated, use `SBDebugger.GetOutputFile`."
) lldb::SBDebugger::GetOutputFileHandle;

%feature("docstring",
    "Deprecated, use `SBDebugger.GetErrorFile`."
) lldb::SBDebugger::GetErrorFileHandle;

%feature("docstring",
    "Deprecated, has no effect."
) lldb::SBDebugger::GetCloseInputOnEOF;

%feature("docstring",
    "Deprecated, has no effect."
) lldb::SBDebugger::SetCloseInputOnEOF;

%feature("docstring",
    "Saves the terminal state of the input file so it can be restored later.

    This is needed by programs that hand the terminal to the debugged process
    and want to restore their own terminal settings afterwards. See
    `SBDebugger.RestoreInputTerminalState`."
) lldb::SBDebugger::SaveInputTerminalState;

%feature("docstring",
    "Restores the terminal state that `SBDebugger.SaveInputTerminalState`
    saved."
) lldb::SBDebugger::RestoreInputTerminalState;

%feature("docstring",
    "Returns the `SBCommandInterpreter` of this debugger.

    Use it to run LLDB command line commands from a script and to add custom
    commands::

        result = lldb.SBCommandReturnObject()
        debugger.GetCommandInterpreter().HandleCommand('breakpoint list', result)
        print(result.GetOutput())
"
) lldb::SBDebugger::GetCommandInterpreter;

%feature("docstring",
    "Runs a single LLDB command line command.

    The output of the command goes to the debugger's output file. Use
    `SBCommandInterpreter.HandleCommand` to capture the output instead::

        debugger.HandleCommand('breakpoint set --name main')
"
) lldb::SBDebugger::HandleCommand;

%feature("docstring",
    "Asks the debugger to interrupt what it is currently doing.

    This is the programmatic equivalent of pressing ``Ctrl+C`` in the command
    line interface: long running operations check
    `SBDebugger.InterruptRequested` and abort. Pair this with
    `SBDebugger.CancelInterruptRequest`."
) lldb::SBDebugger::RequestInterrupt;

%feature("docstring",
    "Cancels a pending interrupt request, see
    `SBDebugger.RequestInterrupt`."
) lldb::SBDebugger::CancelInterruptRequest;

%feature("docstring",
    "Returns whether an interrupt was requested.

    Long running scripts should poll this and stop what they are doing when it
    returns ``True``. See `SBDebugger.RequestInterrupt`."
) lldb::SBDebugger::InterruptRequested;

%feature("docstring",
    "Returns the `SBListener` of this debugger.

    This is the listener that receives the events of all processes of this
    debugger unless a different listener was passed when the process was
    created::

        event = lldb.SBEvent()
        if debugger.GetListener().WaitForEvent(1, event):
            print(lldb.SBDebugger.StateAsCString(lldb.SBProcess.GetStateFromEvent(event)))
"
) lldb::SBDebugger::GetListener;

%feature("docstring",
    "Handles a process event the way the ``lldb`` driver would.

    Prints the process output and a description of the stop to the given output
    and error `SBFile` objects. Useful when implementing a custom event loop
    that should behave like the command line interface."
) lldb::SBDebugger::HandleProcessEvent;

%feature("docstring",
    "Creates a target from an executable file.

    The overloads allow specifying the architecture and platform of the target
    and whether dependent modules (shared libraries) should be loaded::

        target = debugger.CreateTarget('/path/to/a.out')

    Passing an empty path creates a target without an executable, which is
    useful when attaching to a process by name or PID. See
    `SBDebugger.CreateTargetWithFileAndArch` for a variant that only takes an
    architecture, and `SBTarget` for what to do with the result."
) lldb::SBDebugger::CreateTarget;

%feature("docstring",
    "Creates a target from an executable file and a target triple.

    ``target_triple`` is a triple such as ``x86_64-apple-macosx``. See
    `SBDebugger.CreateTarget`."
) lldb::SBDebugger::CreateTargetWithFileAndTargetTriple;

%feature("docstring",
    "Creates a target from an executable file and an architecture name.

    ``archname`` is an architecture such as ``x86_64`` or ``arm64``; pass
    ``lldb.LLDB_ARCH_DEFAULT`` for the host architecture::

        target = debugger.CreateTargetWithFileAndArch(exe, lldb.LLDB_ARCH_DEFAULT)
"
) lldb::SBDebugger::CreateTargetWithFileAndArch;

%feature("docstring",
    "The dummy target holds breakpoints and breakpoint names that will prime newly created targets."
) lldb::SBDebugger::GetDummyTarget;

%feature("docstring",
    "Sends telemetry data about the client to LLDB's telemetry system.

    Only has an effect if LLDB was built with telemetry support."
) lldb::SBDebugger::DispatchClientTelemetry;

%feature("docstring",
    "Return true if target is deleted from the target list of the debugger."
) lldb::SBDebugger::DeleteTarget;

%feature("docstring",
    "Returns the target at the given index.

    In Python, iterating over the debugger yields all of its targets."
) lldb::SBDebugger::GetTargetAtIndex;

%feature("docstring",
    "Returns the index of the given target, or ``lldb.UINT32_MAX`` if the
    target does not belong to this debugger."
) lldb::SBDebugger::GetIndexOfTarget;

%feature("docstring",
    "Returns the target whose process has the given process ID.

    Returns an invalid target if no target of this debugger has such a
    process."
) lldb::SBDebugger::FindTargetWithProcessID;

%feature("docstring",
    "Returns the target with the given executable file name and architecture.

    Either argument may be ``None`` to match any file name or architecture."
) lldb::SBDebugger::FindTargetWithFileAndArch;

%feature("docstring",
    "Returns the target with the given globally unique ID.

    See `SBTarget.GetGloballyUniqueID`."
) lldb::SBDebugger::FindTargetByGloballyUniqueID;

%feature("docstring",
    "Returns the number of targets of this debugger.

    In Python this is also what ``len()`` returns."
) lldb::SBDebugger::GetNumTargets;

%feature("docstring",
    "Returns the currently selected target.

    The selected target is the one that commands operate on by default. See
    `SBDebugger.SetSelectedTarget`."
) lldb::SBDebugger::GetSelectedTarget;

%feature("docstring",
    "Selects the given target, see `SBDebugger.GetSelectedTarget`."
) lldb::SBDebugger::SetSelectedTarget;

%feature("docstring",
    "Returns the currently selected `SBPlatform`.

    New targets use the selected platform, which decides for example whether
    the program is debugged locally or on a remote system."
) lldb::SBDebugger::GetSelectedPlatform;

%feature("docstring",
    "Selects the given `SBPlatform`, see
    `SBDebugger.GetSelectedPlatform`."
) lldb::SBDebugger::SetSelectedPlatform;

%feature("docstring",
    "Get the number of currently active platforms."
) lldb::SBDebugger::GetNumPlatforms;

%feature("docstring",
    "Get one of the currently active platforms."
) lldb::SBDebugger::GetPlatformAtIndex;

%feature("docstring",
    "Get the number of available platforms."
) lldb::SBDebugger::GetNumAvailablePlatforms;

%feature("docstring", "
    Get the name and description of one of the available platforms.

    :param idx: Zero-based index of the platform for which info should be
        retrieved, must be less than the value returned by
        `SBDebugger.GetNumAvailablePlatforms`.
    :rtype: SBStructuredData"
) lldb::SBDebugger::GetAvailablePlatformInfoAtIndex;

%feature("docstring",
    "Returns the `SBSourceManager` of this debugger.

    It provides access to the source files of all targets and caches them."
) lldb::SBDebugger::GetSourceManager;

%feature("docstring",
    "Selects the platform with the given name and returns an `SBError`.

    This is what the ``platform select`` command does, e.g. with
    ``remote-linux``."
) lldb::SBDebugger::SetCurrentPlatform;

%feature("docstring",
    "Sets the SDK root of the currently selected platform.

    Used to find modules and headers of a specific SDK when debugging a remote
    system."
) lldb::SBDebugger::SetCurrentPlatformSDKRoot;

%feature("docstring",
    "Sets whether the ``source edit`` command opens an external editor."
) lldb::SBDebugger::SetUseExternalEditor;

%feature("docstring",
    "Returns whether an external editor is used, see
    `SBDebugger.SetUseExternalEditor`."
) lldb::SBDebugger::GetUseExternalEditor;

%feature("docstring",
    "Sets whether the debugger colorizes its output with ANSI escape codes."
) lldb::SBDebugger::SetUseColor;

%feature("docstring",
    "Returns whether colorized output is used, see
    `SBDebugger.SetUseColor`."
) lldb::SBDebugger::GetUseColor;

%feature("docstring",
    "Sets whether diagnostics are shown inline with the source code."
) lldb::SBDebugger::SetShowInlineDiagnostics;

%feature("docstring",
    "Sets whether source files are cached in memory.

    Caching makes repeatedly showing source faster but uses more memory and
    does not notice when a file changes on disk."
) lldb::SBDebugger::SetUseSourceCache;

%feature("docstring",
    "Returns whether the source cache is used, see
    `SBDebugger.SetUseSourceCache`."
) lldb::SBDebugger::GetUseSourceCache;

%feature("docstring",
    "Returns the default architecture that new targets use.

    In Python this takes no arguments and returns the architecture name as a
    string."
) lldb::SBDebugger::GetDefaultArchitecture;

%feature("docstring",
    "Sets the default architecture that new targets use, e.g. ``x86_64``."
) lldb::SBDebugger::SetDefaultArchitecture;

%feature("docstring",
    "Returns the ``lldb.eScriptLanguage*`` enumerator for a language name such
    as ``python``."
) lldb::SBDebugger::GetScriptingLanguage;

%feature("docstring",
    "Returns information about a script interpreter as `SBStructuredData`.

    ``language`` is one of the ``lldb.eScriptLanguage*`` enumerators. The data
    contains for example the language version LLDB was built against."
) lldb::SBDebugger::GetScriptInterpreterInfo;

%feature("docstring",
    "Returns the LLDB version as a string."
) lldb::SBDebugger::GetVersionString;

%feature("docstring",
    "Returns a human readable name for a process state.

    ``state`` is one of the ``lldb.eState*`` enumerators, see
    `SBProcess.GetState`::

        print(lldb.SBDebugger.StateAsCString(process.GetState()))
"
) lldb::SBDebugger::StateAsCString;

%feature("docstring",
    "Returns how LLDB was built as `SBStructuredData`.

    Contains for example whether XML, Python or curses support was enabled."
) lldb::SBDebugger::GetBuildConfiguration;

%feature("docstring",
    "Returns whether the given process state means the process is running.

    See `SBProcess.GetState`."
) lldb::SBDebugger::StateIsRunningState;

%feature("docstring",
    "Returns whether the given process state means the process is stopped.

    Note that states such as ``lldb.eStateExited`` also count as stopped. See
    `SBProcess.GetState`."
) lldb::SBDebugger::StateIsStoppedState;

%feature("docstring",
    "Enables logging for a channel and returns whether that succeeded.

    ``channel`` is a log channel such as ``lldb`` or ``dwarf`` and
    ``categories`` a list of category names, as used by the ``log enable``
    command::

        debugger.EnableLog('lldb', ['break', 'target'])

    The log output goes to the debugger's output file unless
    `SBDebugger.SetLoggingCallback` is used."
) lldb::SBDebugger::EnableLog;

%feature("docstring",
    "Redirects LLDB's log output to a Python callable.

    The callable is invoked with each log message as its only argument::

        debugger.SetLoggingCallback(lambda message: print(message, end=''))
"
) lldb::SBDebugger::SetLoggingCallback;

%feature("docstring",
    "Deprecated, use `SBDebugger.AddDestroyCallback` and
    `SBDebugger.RemoveDestroyCallback`."
) lldb::SBDebugger::SetDestroyCallback;

%feature("docstring",
    "Registers a callable that is invoked when this debugger is destroyed.

    Returns a token that can be passed to
    `SBDebugger.RemoveDestroyCallback`. Use this to clean up resources that a
    script associated with a debugger."
) lldb::SBDebugger::AddDestroyCallback;

%feature("docstring",
    "Removes a destroy callback that `SBDebugger.AddDestroyCallback`
    registered.

    ``token`` is the value that registering the callback returned. Returns
    whether a callback with that token existed."
) lldb::SBDebugger::RemoveDestroyCallback;

%feature("docstring",
    "Feeds data to the debugger's command interpreter as if it was typed.

    In Python the ``data`` and ``data_len`` parameters of the C++ API are
    replaced by a single ``bytes``-like object. See
    `SBDebugger.SetInputString` for a simpler alternative."
) lldb::SBDebugger::DispatchInput;

%feature("docstring",
    "Tells the debugger's input handler that an interrupt was received.

    This has the same effect as pressing ``Ctrl+C`` while the debugger reads
    input."
) lldb::SBDebugger::DispatchInputInterrupt;

%feature("docstring",
    "Tells the debugger's input handler that the input ended.

    This has the same effect as pressing ``Ctrl+D`` while the debugger reads
    input."
) lldb::SBDebugger::DispatchInputEndOfFile;

%feature("docstring",
    "Returns the name of this debugger instance, e.g. ``debugger_1``.

    The instance name is used to address a specific debugger in settings."
) lldb::SBDebugger::GetInstanceName;

%feature("docstring",
    "Returns the debugger with the given ID.

    The ID is the one `SBDebugger.GetID` returns. Returns an invalid debugger if
    there is no such debugger in this process."
) lldb::SBDebugger::FindDebuggerWithID;

%feature("docstring",
    "Sets a debugger setting and returns an `SBError`.

    This is the programmatic version of the ``settings set`` command.
    ``debugger_instance_name`` is the name of the debugger the setting applies
    to, see `SBDebugger.GetInstanceName`::

        lldb.SBDebugger.SetInternalVariable('target.x86-disassembly-flavor',
                                            'intel', debugger.GetInstanceName())
"
) lldb::SBDebugger::SetInternalVariable;

%feature("docstring",
    "Returns the value of a debugger setting as an `SBStringList`.

    This is the programmatic version of the ``settings show`` command, see
    `SBDebugger.SetInternalVariable`. `SBDebugger.GetSetting` returns settings
    as structured data instead."
) lldb::SBDebugger::GetInternalVariableValue;

%feature("docstring",
    "Writes a description of this debugger into the given `SBStream`."
) lldb::SBDebugger::GetDescription;

%feature("docstring",
    "Returns the width in characters the debugger assumes its terminal has."
) lldb::SBDebugger::GetTerminalWidth;

%feature("docstring",
    "Deprecated, use `SBDebugger.SetTerminalDimensions`."
) lldb::SBDebugger::SetTerminalWidth;

%feature("docstring",
    "Returns the height in lines the debugger assumes its terminal has."
) lldb::SBDebugger::GetTerminalHeight;

%feature("docstring",
    "Deprecated, use `SBDebugger.SetTerminalDimensions`."
) lldb::SBDebugger::SetTerminalHeight;

%feature("docstring",
    "Tells the debugger how large its terminal is.

    The dimensions are used to lay out output such as tables and the disassembly
    of the command line interface."
) lldb::SBDebugger::SetTerminalDimensions;

%feature("docstring",
    "Returns the unique ID of this debugger instance.

    Pass it to `SBDebugger.FindDebuggerWithID` to get the debugger back."
) lldb::SBDebugger::GetID;

%feature("docstring",
    "Returns the prompt of the command line interface.

    The default prompt is ``(lldb)`` followed by a space."
) lldb::SBDebugger::GetPrompt;

%feature("docstring",
    "Sets the prompt of the command line interface."
) lldb::SBDebugger::SetPrompt;

%feature("docstring",
    "Returns the path of the reproducer, if one is being captured."
) lldb::SBDebugger::GetReproducerPath;

%feature("docstring",
    "Returns the scripting language of this debugger as one of the
    ``lldb.eScriptLanguage*`` enumerators."
) lldb::SBDebugger::GetScriptLanguage;

%feature("docstring",
    "Sets the scripting language of this debugger.

    ``script_lang`` is one of the ``lldb.eScriptLanguage*`` enumerators and
    decides which language the ``script`` command and script breakpoint
    callbacks use."
) lldb::SBDebugger::SetScriptLanguage;

%feature("docstring",
    "Returns the language the ``repl`` command uses as one of the
    ``lldb.eLanguageType*`` enumerators."
) lldb::SBDebugger::GetREPLLanguage;

%feature("docstring",
    "Sets the language the ``repl`` command uses, see
    `SBDebugger.GetREPLLanguage`."
) lldb::SBDebugger::SetREPLLanguage;

%feature("docstring",
    "Returns the data formatter category with the given name or language.

    Categories group the type formatters, summaries, filters and synthetic
    children providers that LLDB uses to display values, see
    `SBTypeCategory`::

        category = debugger.GetCategory('MyFormatters')
"
) lldb::SBDebugger::GetCategory;

%feature("docstring",
    "Creates a new data formatter category with the given name.

    The category is disabled at first, see
    `SBTypeCategory.SetEnabled`."
) lldb::SBDebugger::CreateCategory;

%feature("docstring",
    "Deletes the data formatter category with the given name and returns
    whether it existed."
) lldb::SBDebugger::DeleteCategory;

%feature("docstring",
    "Returns the number of data formatter categories, see
    `SBDebugger.GetCategoryAtIndex`."
) lldb::SBDebugger::GetNumCategories;

%feature("docstring",
    "Returns the data formatter category at the given index as an
    `SBTypeCategory`."
) lldb::SBDebugger::GetCategoryAtIndex;

%feature("docstring",
    "Returns the default data formatter category.

    This is the category named ``default``, which is where formatters end up
    that are added without specifying a category."
) lldb::SBDebugger::GetDefaultCategory;

%feature("docstring",
    "Returns the `SBTypeFormat` that applies to the given type name.

    ``type_name_spec`` is an `SBTypeNameSpecifier`. Returns an invalid format if
    no format applies to that type."
) lldb::SBDebugger::GetFormatForType;

%feature("docstring",
    "Returns the `SBTypeSummary` that applies to the given type name.

    See `SBDebugger.GetFormatForType`."
) lldb::SBDebugger::GetSummaryForType;

%feature("docstring",
    "Returns the `SBTypeFilter` that applies to the given type name.

    See `SBDebugger.GetFormatForType`."
) lldb::SBDebugger::GetFilterForType;

%feature("docstring",
    "Returns the `SBTypeSynthetic` that applies to the given type name.

    See `SBDebugger.GetFormatForType`."
) lldb::SBDebugger::GetSyntheticForType;

%feature("docstring",
    "Discards the statistics that were collected for the targets of this
    debugger.

    See `SBTarget.GetStatistics`."
) lldb::SBDebugger::ResetStatistics;

%feature("docstring",
"Launch a command interpreter session. Commands are read from standard input or
from the input handle specified for the debugger object. Output/errors are
similarly redirected to standard output/error or the configured handles.

:param auto_handle_events: If true, automatically handle resulting events.
:param spawn_thread: If true, start a new thread for IO handling.
:param options: Parameter collection of type `SBCommandInterpreterRunOptions`.
:param num_errors: Initial error counter.
:param quit_requested: Initial quit request flag.
:param stopped_for_crash: Initial crash flag.

:return:
    A tuple with the number of errors encountered by the interpreter, a boolean
    indicating whether quitting the interpreter was requested and another boolean
    set to True in case of a crash.

Example: ::

    # Start an interactive lldb session from a script (with a valid debugger object
    # created beforehand):
    n_errors, quit_requested, has_crashed = debugger.RunCommandInterpreter(True,
        False, lldb.SBCommandInterpreterRunOptions(), 0, False, False)"
) lldb::SBDebugger::RunCommandInterpreter;

%feature("docstring",
    "Runs a read-eval-print loop for the given language.

    ``language`` is one of the ``lldb.eLanguageType*`` enumerators and
    ``repl_options`` are options that are passed to the REPL implementation.
    Returns an `SBError` if no REPL is available for that language."
) lldb::SBDebugger::RunREPL;

%feature("docstring",
    "Loads a processor trace from a trace description file.

    Creates the targets, processes and threads that are described in the file
    and returns the resulting `SBTrace`. ``error`` is filled in if the trace
    could not be loaded. See `SBTarget.GetTrace`."
) lldb::SBDebugger::LoadTraceFromFile;
