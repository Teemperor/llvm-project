%feature("docstring",
"Represents the process of the program that is being debugged.

A process is created by launching or attaching to a program with an
`SBTarget` (`SBTarget.Launch`, `SBTarget.LaunchSimple`,
`SBTarget.Attach`, `SBTarget.AttachToProcessWithID`), or by loading a core
file with `SBTarget.LoadCore`. `SBTarget.GetProcess` returns the process of a
target.

The process is what execution is controlled through
(`SBProcess.Continue`, `SBProcess.Stop`, `SBProcess.Kill`,
`SBProcess.Detach`), and it owns the threads of the program
(`SBProcess.GetThreadAtIndex`, `SBProcess.GetSelectedThread`) as well as its
memory (`SBProcess.ReadMemory`, `SBProcess.WriteMemory`).

`SBProcess.GetState` describes what the process is currently doing; most API
functions require a process that is stopped (``lldb.eStateStopped``). Every
state change is also broadcast as an event, see `SBListener` and
`SBProcess.GetBroadcaster` for how to wait for them asynchronously.

In Python an SBProcess is iterable and yields its threads::

    def get_stopped_threads(process, reason):
        '''Returns the thread(s) with the specified stop reason in a list.'''
        return [t for t in process if t.GetStopReason() == reason]

For example, running a program until it stops and printing where it stopped::

    process = target.LaunchSimple(None, None, os.getcwd())
    if process.GetState() == lldb.eStateStopped:
        thread = process.GetSelectedThread()
        print('stopped in %s' % thread.GetFrameAtIndex(0).GetFunctionName())

See also :py:class:`SBThread`, :py:class:`SBTarget` and
:py:class:`SBMemoryRegionInfo`."
) lldb::SBProcess;

%feature("docstring", "
    Returns the name of the broadcaster class that sends process events
    (``lldb.process``).

    Pass this to `SBListener.StartListeningForEventClass` to receive process
    events before a process even exists. See also
    `SBProcess.GetBroadcasterClass`."
) lldb::SBProcess::GetBroadcasterClassName;

%feature("docstring", "
    Returns the name of the process plugin that is used for this process.

    Examples are ``gdb-remote`` for processes debugged through a remote
    debug server and ``mach-o-core`` or ``minidump`` for core files."
) lldb::SBProcess::GetPluginName;

%feature("docstring", "
    Resets this object to an invalid process."
) lldb::SBProcess::Clear;

%feature("docstring", "
    Returns whether this object refers to a process.

    Note that a valid process can still have exited, use
    `SBProcess.GetState` to find out whether it is still alive."
) lldb::SBProcess::IsValid;

%feature("docstring", "
    Returns the `SBTarget` this process belongs to."
) lldb::SBProcess::GetTarget;

%feature("docstring", "
    Returns the byte order of the process as one of the ``lldb.eByteOrder*``
    enumerators."
) lldb::SBProcess::GetByteOrder;

%feature("docstring", "
    Writes data into the current process's stdin. API client specifies a Python
    string as the only argument."
) lldb::SBProcess::PutSTDIN;

%feature("docstring", "
    Reads data from the current process's stdout stream. API client specifies
    the size of the buffer to read data into. It returns the byte buffer in a
    Python string.

    Output is buffered, so reading returns whatever the process has written
    since the last read, up to the given size::

        while True:
            out = process.GetSTDOUT(1024)
            if not out:
                break
            print(out, end='')

    Note that this only works if the process was launched without redirecting
    its output to a file or terminal, see `SBLaunchInfo.AddOpenFileAction`."
) lldb::SBProcess::GetSTDOUT;

%feature("docstring", "
    Reads data from the current process's stderr stream. API client specifies
    the size of the buffer to read data into. It returns the byte buffer in a
    Python string."
) lldb::SBProcess::GetSTDERR;

%feature("docstring", "
    Reads profiling data that the process plugin collected asynchronously.

    Only some platforms produce profile data. The data is also delivered as
    ``lldb.SBProcess.eBroadcastBitProfileData`` events."
) lldb::SBProcess::GetAsyncProfileData;

%feature("docstring", "
    Writes a description of the process state change in the given `SBEvent` to
    the given file.

    This is a convenience function for event loops that want to print the same
    information the ``lldb`` driver prints when a process stops."
) lldb::SBProcess::ReportEventState;

%feature("docstring", "
    Appends a description of the process state change in the given `SBEvent` to
    the given `SBStream`.

    Like `SBProcess.ReportEventState`, but the description is written into a
    stream instead of a file."
) lldb::SBProcess::AppendEventStateReport;

%feature("docstring",
"See SBTarget.Launch for argument description and usage."
) lldb::SBProcess::RemoteLaunch;

%feature("docstring", "
    Remote connection related functions. These will fail if the
    process is not in eStateConnected. They are intended for use
    when connecting to an externally managed debugserver instance."
) lldb::SBProcess::RemoteAttachToProcessWithID;

%feature("docstring", "
    Returns the number of threads of this process.

    The number of threads is only meaningful while the process is stopped; see
    `SBProcess.GetThreadAtIndex` to access them."
) lldb::SBProcess::GetNumThreads;

%feature("docstring", "
    Returns the INDEX'th thread from the list of current threads.  The index
    of a thread is only valid for the current stop.  For a persistent thread
    identifier use either the thread ID or the IndexID.  See help on `SBThread`
    for more details."
) lldb::SBProcess::GetThreadAtIndex;

%feature("docstring", "
    Returns the thread with the given thread ID.

    The thread ID is the system's thread identifier, see
    `SBThread.GetThreadID`. Returns an invalid thread if the process has no
    such thread."
) lldb::SBProcess::GetThreadByID;

%feature("docstring", "
    Returns the thread with the given thread IndexID.

    See `SBThread.GetIndexID` for what an index ID is."
) lldb::SBProcess::GetThreadByIndexID;

%feature("docstring", "
    Returns the currently selected thread.

    The selected thread is the one that commands such as ``thread backtrace``
    operate on. After a stop LLDB selects the thread that caused the stop. See
    `SBProcess.SetSelectedThread`."
) lldb::SBProcess::GetSelectedThread;

%feature("docstring", "
    Lazily create a thread on demand through the current OperatingSystem plug-in, if the current OperatingSystem plug-in supports it."
) lldb::SBProcess::CreateOSPluginThread;

%feature("docstring", "
    Selects the given thread and returns whether that succeeded.

    See `SBProcess.GetSelectedThread`, and
    `SBProcess.SetSelectedThreadByID` and
    `SBProcess.SetSelectedThreadByIndexID` to select a thread by identifier."
) lldb::SBProcess::SetSelectedThread;

%feature("docstring", "
    Selects the thread with the given thread ID, see
    `SBProcess.SetSelectedThread`."
) lldb::SBProcess::SetSelectedThreadByID;

%feature("docstring", "
    Selects the thread with the given index ID, see
    `SBProcess.SetSelectedThread`."
) lldb::SBProcess::SetSelectedThreadByIndexID;

%feature("docstring", "
    Returns the number of libdispatch (Grand Central Dispatch) queues of this
    process.

    Returns ``0`` on systems and processes that don't use libdispatch. See
    `SBQueue`."
) lldb::SBProcess::GetNumQueues;

%feature("docstring", "
    Returns the libdispatch queue at the given index as an `SBQueue`.

    See `SBProcess.GetNumQueues`."
) lldb::SBProcess::GetQueueAtIndex;

%feature("docstring", "
    Returns what the process is currently doing as one of the ``lldb.eState*``
    enumerators.

    The most important values are ``lldb.eStateStopped`` (the process is
    stopped and can be inspected), ``lldb.eStateRunning``,
    ``lldb.eStateExited`` and ``lldb.eStateCrashed``::

        if process.GetState() == lldb.eStateExited:
            print('exited with status %d' % process.GetExitStatus())

    `SBDebugger.StateAsCString` turns a state into a human readable string and
    `SBDebugger.StateIsStoppedState` tells whether a state means the process is
    stopped."
) lldb::SBProcess::GetState;

%feature("docstring", "
    Returns the exit status of the process.

    Only meaningful once the process state is ``lldb.eStateExited``. See
    `SBProcess.GetExitDescription` for a textual description of why the process
    exited."
) lldb::SBProcess::GetExitStatus;

%feature("docstring", "
    Returns a description of why the process exited, if the platform provides
    one.

    Returns ``None`` for a normal exit. See `SBProcess.GetExitStatus`."
) lldb::SBProcess::GetExitDescription;

%feature("docstring", "
    Returns the process ID of the process."
) lldb::SBProcess::GetProcessID;

%feature("docstring", "
    Returns an integer ID that is guaranteed to be unique across all process instances. This is not the process ID, just a unique integer for comparison and caching purposes."
) lldb::SBProcess::GetUniqueID;

%feature("docstring", "
    Returns the size in bytes of an address in this process, usually ``4`` or
    ``8``."
) lldb::SBProcess::GetAddressByteSize;

%feature("docstring", "
    Kills the process and shuts down all threads that were spawned to
    track and monitor process."
) lldb::SBProcess::Destroy;

%feature("docstring", "
    Resumes execution of the process and returns an `SBError`.

    The process runs until it stops again, for example because a breakpoint was
    hit, a signal arrived or the program exited. By default this returns as soon
    as the process has been resumed, so waiting for the next stop has to be done
    separately, either through the event system (`SBListener.WaitForEvent`) or by
    running the debugger in synchronous mode
    (`SBDebugger.SetAsync` with ``False``), in which case Continue only returns
    once the process stopped again::

        debugger.SetAsync(False)
        process.Continue()
        print('stopped again: %s' % process.GetSelectedThread().GetStopDescription(256))
"
) lldb::SBProcess::Continue;

%feature("docstring", "
    Resumes execution of the process in the given direction.

    ``direction`` is one of the ``lldb.eRunForward`` or
    ``lldb.eRunReverse`` enumerators. Reverse execution requires a process
    plugin that supports it, such as a connection to the ``rr`` replay
    debugger. See `SBProcess.Continue`."
) lldb::SBProcess::ContinueInDirection;

%feature("docstring", "
    Stops the process and returns an `SBError`.

    Requests that a running process is halted. Note that the process is not
    necessarily stopped when this call returns; wait for the stopped event to be
    sure. See `SBProcess.SendAsyncInterrupt` for interrupting a process from
    another thread."
) lldb::SBProcess::Stop;

%feature("docstring", "Same as Destroy(self).") lldb::SBProcess::Kill;

%feature("docstring", "
    Detaches from the process and returns an `SBError`.

    The process keeps running without the debugger attached. If
    ``keep_stopped`` is ``True`` the process is left stopped instead, which is
    only supported by some platforms. See `SBProcess.Destroy` to kill the
    process instead."
) lldb::SBProcess::Detach;

%feature("docstring", "Sends the process a unix signal.") lldb::SBProcess::Signal;

%feature("docstring", "
    Returns the `SBUnixSignals` of this process.

    Use it to control what LLDB does when the process receives a specific
    signal, for example to pass a signal through to the process without
    stopping::

        process.GetUnixSignals().SetShouldStop(signal.SIGPIPE, False)
"
) lldb::SBProcess::GetUnixSignals;

%feature("docstring", "
    Asks the process to stop, without waiting for it to actually stop.

    Unlike `SBProcess.Stop` this can be called while the process is running
    from a different thread than the one that runs the debugger's event loop.
    The stop shows up as a process event with
    `SBProcess.GetInterruptedFromEvent` returning ``True``."
) lldb::SBProcess::SendAsyncInterrupt;

%feature("docstring", "
    Returns a stop id that will increase every time the process executes.  If
    include_expression_stops is true, then stops caused by expression evaluation
    will cause the returned value to increase, otherwise the counter returned will
    only increase when execution is continued explicitly by the user.  Note, the value
    will always increase, but may increase by more than one per stop."
) lldb::SBProcess::GetStopID;

%feature("docstring", "
    Returns the `SBEvent` of the stop with the given stop ID.

    Note that this is only implemented for the most recent natural stop, see
    `SBProcess.GetStopID`."
) lldb::SBProcess::GetStopEventForStopID;

%feature("docstring", "
    Sets the state of a scripted process, and does nothing for other processes.

    Only useful when implementing a scripted process in Python, see
    :doc:`/use/python-reference`."
) lldb::SBProcess::ForceScriptedState;

%feature("docstring", "
    Reads memory from the current process's address space and removes any
    traps that may have been inserted into the memory. It returns the byte
    buffer in a Python string. Example: ::

        # Read 4 bytes from address 'addr' and assume error.Success() is True.
        content = process.ReadMemory(addr, 4, error)
        new_bytes = bytearray(content)

    See `SBProcess.WriteMemory` for the opposite operation, and
    `SBTarget.ReadMemory` for reading memory of a target that has no running
    process."
) lldb::SBProcess::ReadMemory;

%feature("docstring", "
    Writes memory to the current process's address space and maintains any
    traps that might be present due to software breakpoints. Example: ::

        # Create a Python string from the byte array.
        new_value = str(bytes)
        result = process.WriteMemory(addr, new_value, error)
        if not error.Success() or result != len(bytes):
            print('SBProcess.WriteMemory() failed!')"
) lldb::SBProcess::WriteMemory;

%feature("docstring", "
    Reads a NUL terminated C string from the current process's address space.
    It returns a python string of the exact length, or truncates the string if
    the maximum character limit is reached. Example: ::

        # Read a C string of at most 256 bytes from address '0x1000'
        error = lldb.SBError()
        cstring = process.ReadCStringFromMemory(0x1000, 256, error)
        if error.Success():
            print('cstring: ', cstring)
        else
            print('error: ', error)"
) lldb::SBProcess::ReadCStringFromMemory;


%feature("docstring", "
    Reads an unsigned integer from memory given a byte size and an address.
    Returns the unsigned integer that was read. Example: ::

        # Read a 4 byte unsigned integer from address 0x1000
        error = lldb.SBError()
        uint = ReadUnsignedFromMemory(0x1000, 4, error)
        if error.Success():
            print('integer: %u' % uint)
        else
            print('error: ', error)"
) lldb::SBProcess::ReadUnsignedFromMemory;


%feature("docstring", "
    Reads a pointer from memory from an address and returns the value. Example: ::

        # Read a pointer from address 0x1000
        error = lldb.SBError()
        ptr = ReadPointerFromMemory(0x1000, error)
        if error.Success():
            print('pointer: 0x%x' % ptr)
        else
            print('error: ', error)"
) lldb::SBProcess::ReadPointerFromMemory;

%feature("docstring", "
    Searches the given address ranges for a byte pattern and returns all
    matches.

    ``buf`` is the pattern to search for (in Python a ``bytes`` object),
    ``ranges`` is an `SBAddressRangeList` of the memory to search, ``alignment``
    requires matches to start at a multiple of that value and ``max_matches``
    limits how many matches are returned. Returns an `SBAddressRangeList` with
    one range per match::

        ranges = lldb.SBAddressRangeList()
        ranges.Append(some_range)
        error = lldb.SBError()
        matches = process.FindRangesInMemory(b'needle', ranges, 1, 10, error)

    See `SBProcess.FindInMemory` to search a single range for the first match."
) lldb::SBProcess::FindRangesInMemory;

%feature("docstring", "
    Searches a single address range for a byte pattern.

    Returns the load address of the first match or
    ``lldb.LLDB_INVALID_ADDRESS`` if the pattern was not found. See
    `SBProcess.FindRangesInMemory`."
) lldb::SBProcess::FindInMemory;

%feature("docstring", "
    Returns the process state a process event describes.

    This is how the state of a process is inspected when handling events
    asynchronously::

        if lldb.SBProcess.EventIsProcessEvent(event):
            state = lldb.SBProcess.GetStateFromEvent(event)

    See `SBListener` for how to receive events."
) lldb::SBProcess::GetStateFromEvent;

%feature("docstring", "
    Returns whether the process was restarted while handling a stop event.

    This happens for example when a breakpoint's condition or callback resumes
    the process, in which case the stop event should usually be ignored."
) lldb::SBProcess::GetRestartedFromEvent;

%feature("docstring", "
    Returns the number of reasons why the process was restarted, see
    `SBProcess.GetRestartedFromEvent`."
) lldb::SBProcess::GetNumRestartedReasonsFromEvent;

%feature("docstring", "
    Returns one of the reasons why the process was restarted as a string.

    See `SBProcess.GetNumRestartedReasonsFromEvent`."
) lldb::SBProcess::GetRestartedReasonAtIndexFromEvent;

%feature("docstring", "
    Returns the `SBProcess` a process event refers to."
) lldb::SBProcess::GetProcessFromEvent;

%feature("docstring", "
    Returns whether the stop this event describes was caused by an interrupt.

    See `SBProcess.SendAsyncInterrupt`."
) lldb::SBProcess::GetInterruptedFromEvent;

%feature("docstring", "
    Returns the `SBStructuredData` of a structured data event.

    Structured data events are sent by process plugins to report information
    that has no dedicated event type, see
    `SBProcess.EventIsStructuredDataEvent`."
) lldb::SBProcess::GetStructuredDataFromEvent;

%feature("docstring", "
    Returns whether the given `SBEvent` is a process event."
) lldb::SBProcess::EventIsProcessEvent;

%feature("docstring", "
    Returns whether the given `SBEvent` is a structured data event, see
    `SBProcess.GetStructuredDataFromEvent`."
) lldb::SBProcess::EventIsStructuredDataEvent;

%feature("docstring", "
    Returns the `SBBroadcaster` of this process.

    Add it to an `SBListener` to receive the process's events::

        listener = lldb.SBListener('my listener')
        process.GetBroadcaster().AddListener(listener,
                                            lldb.SBProcess.eBroadcastBitStateChanged)
"
) lldb::SBProcess::GetBroadcaster;

%feature("docstring", "Get default process broadcaster class name (lldb.process)."
) lldb::SBProcess::GetBroadcasterClass;

%feature("docstring", "
    Writes a description of this process into the given `SBStream`.

    See `SBProcess.GetStatus` for the more detailed output of the ``process
    status`` command."
) lldb::SBProcess::GetDescription;

%feature("docstring", "
    Returns the process' extended crash information."
) lldb::SBProcess::GetExtendedCrashInformation;

%feature("docstring", "
    Returns how many hardware watchpoints this process supports.

    Returns ``0`` and fills in ``error`` if the number can't be determined.
    See `SBTarget.WatchpointCreateByAddress`."
) lldb::SBProcess::GetNumSupportedHardwareWatchpoints;

%feature("docstring", "
    Loads a shared library into this process.

    ``remote_image_spec`` is the path of the library on the target system.
    Returns a token that can be passed to `SBProcess.UnloadImage`, or
    ``lldb.LLDB_INVALID_IMAGE_TOKEN`` if the library could not be loaded, in
    which case ``error`` explains why::

        error = lldb.SBError()
        token = process.LoadImage(lldb.SBFileSpec('/usr/lib/libfoo.dylib'), error)

    See `SBProcess.LoadImageUsingPaths` to search several directories for a
    library."
) lldb::SBProcess::LoadImage;

%feature("docstring", "
    Load the library whose filename is given by image_spec looking in all the
    paths supplied in the paths argument.  If successful, return a token that
    can be passed to UnloadImage and fill loaded_path with the path that was
    successfully loaded.  On failure, return
    lldb.LLDB_INVALID_IMAGE_TOKEN."
) lldb::SBProcess::LoadImageUsingPaths;

%feature("docstring", "
    Unloads a shared library that was loaded with `SBProcess.LoadImage`.

    ``image_token`` is the token that loading the image returned. Returns an
    `SBError` describing any failure."
) lldb::SBProcess::UnloadImage;

%feature("docstring", "
    Sends data to the process plugin of this process.

    What the data means depends entirely on the plugin, so this is only useful
    together with a specific process plugin."
) lldb::SBProcess::SendEventData;

%feature("docstring", "
    Return the number of different thread-origin extended backtraces
    this process can support as a uint32_t.
    When the process is stopped and you have an SBThread, lldb may be
    able to show a backtrace of when that thread was originally created,
    or the work item was enqueued to it (in the case of a libdispatch
    queue)."
) lldb::SBProcess::GetNumExtendedBacktraceTypes;

%feature("docstring", "
    Takes an index argument, returns the name of one of the thread-origin
    extended backtrace methods as a str."
) lldb::SBProcess::GetExtendedBacktraceTypeAtIndex;

%feature("docstring", "
    Returns the threads that previously used the memory at the given address.

    Memory history is provided by instrumentation runtimes such as
    AddressSanitizer or MallocStackLogging: for an address that was allocated
    and freed, this returns the (history) threads that show where the allocation
    and the deallocation happened. Returns an empty `SBThreadCollection` if no
    such information is available."
) lldb::SBProcess::GetHistoryThreads;

%feature("docstring", "
    Returns whether the given instrumentation runtime is used by this process.

    ``type`` is one of the ``lldb.eInstrumentationRuntimeType*`` enumerators,
    for example the AddressSanitizer or ThreadSanitizer runtime."
) lldb::SBProcess::IsInstrumentationRuntimePresent;

%feature("docstring", "
    Saves the state of the process into a core file.

    ``file_name`` is where the core file is written, ``flavor`` selects the core
    file format (currently ``mach-o`` or ``minidump``) and ``core_style`` is one
    of the ``lldb.eSaveCore*`` enumerators that controls how much memory is
    included. The overload that only takes a file name uses the flavor that
    matches the process' main executable::

        error = process.SaveCore('/tmp/core', 'minidump', lldb.eSaveCoreDirtyOnly)

    The overload that takes an `SBSaveCoreOptions` provides the most control
    over what is saved."
) lldb::SBProcess::SaveCore;

%feature("docstring", "
    Returns information about the memory region that contains the given
    address.

    Fills in the given `SBMemoryRegionInfo` and returns an `SBError` describing
    any failure::

        region = lldb.SBMemoryRegionInfo()
        error = process.GetMemoryRegionInfo(0x1000, region)
        if error.Success() and region.IsWritable():
            print('writable region ending at %x' % region.GetRegionEnd())

    See `SBProcess.GetMemoryRegions` to enumerate all regions."
) lldb::SBProcess::GetMemoryRegionInfo;

%feature("docstring", "
    Get a list of all the memory regions associated with this process. ::

        readable_regions = []
        for region in process.GetMemoryRegions():
            if region.IsReadable():
                readable_regions.append(region)

"
) lldb::SBProcess::GetMemoryRegions;

%feature("docstring", "
    Get information about the process.
    Valid process info will only be returned when the process is alive,
    use IsValid() to check if the info returned is valid. ::

        process_info = process.GetProcessInfo()
        if process_info.IsValid():
            process_info.GetProcessID()"
) lldb::SBProcess::GetProcessInfo;

%feature("docstring", "
    Returns the core file this process was loaded from as an `SBFileSpec`.

    Returns an invalid file spec for live processes, see
    `SBTarget.LoadCore` and `SBProcess.IsLiveDebugSession`."
) lldb::SBProcess::GetCoreFile;

%feature("docstring", "
    Returns whether this is a live debug session.

    Returns ``False`` for post-mortem sessions such as core files and
    minidumps, in which the process cannot be resumed and memory cannot be
    written."
) lldb::SBProcess::IsLiveDebugSession;

%feature("docstring", "
    Get the current address mask in this Process of a given type.
    There are lldb.eAddressMaskTypeCode and lldb.eAddressMaskTypeData address
    masks, and on most Targets, the the Data address mask is more general
    because there are no alignment restrictions, as there can be with Code
    addresses.
    lldb.eAddressMaskTypeAny may be used to get the most general mask.
    The bits which are not used for addressing are set to 1 in the returned
    mask.
    In an unusual environment with different address masks for high and low
    memory, this may also be specified.  This is uncommon, default is
    lldb.eAddressMaskRangeLow."
) lldb::SBProcess::GetAddressMask;

%feature("docstring", "
    Set the current address mask in this Process for a given type,
    lldb.eAddressMaskTypeCode or lldb.eAddressMaskTypeData.  Bits that are not
    used for addressing should be set to 1 in the mask.
    When setting all masks, lldb.eAddressMaskTypeAll may be specified.
    In an unusual environment with different address masks for high and low
    memory, this may also be specified.  This is uncommon, default is
    lldb.eAddressMaskRangeLow."
) lldb::SBProcess::SetAddressMask;

%feature("docstring", "
    Set the number of low bits relevant for addressing in this Process 
    for a given type, lldb.eAddressMaskTypeCode or lldb.eAddressMaskTypeData.
    When setting all masks, lldb.eAddressMaskTypeAll may be specified.
    In an unusual environment with different address masks for high and low
    memory, the address range  may also be specified.  This is uncommon, 
    default is lldb.eAddressMaskRangeLow."
) lldb::SBProcess::SetAddressableBits;

%feature("docstring", "
    Given a virtual address, clear the bits that are not used for addressing
    (and may be used for metadata, memory tagging, point authentication, etc).
    By default the most general mask, lldb.eAddressMaskTypeAny is used to 
    process the address, but lldb.eAddressMaskTypeData and 
    lldb.eAddressMaskTypeCode may be specified if the type of address is known."
) lldb::SBProcess::FixAddress;

%feature("docstring", "
    Allocates a block of memory within the process, with size and
    access permissions specified in the arguments. The permissions
    argument is an or-combination of zero or more of
    lldb.ePermissionsWritable, lldb.ePermissionsReadable, and
    lldb.ePermissionsExecutable. Returns the address
    of the allocated buffer in the process, or
    lldb.LLDB_INVALID_ADDRESS if the allocation failed.

    The memory stays allocated until `SBProcess.DeallocateMemory` is called or
    the process exits::

        error = lldb.SBError()
        addr = process.AllocateMemory(0x1000,
                                      lldb.ePermissionsReadable | lldb.ePermissionsWritable,
                                      error)
"
) lldb::SBProcess::AllocateMemory;

%feature("docstring", "
    Deallocates the block of memory (previously allocated using
    AllocateMemory) given in the argument."
) lldb::SBProcess::DeallocateMemory;

%feature("docstring", "
    Returns the implementation object of the process plugin if available. None
    otherwise.

    For scripted processes this is the `SBScriptObject` wrapping the Python
    object that implements the process."
) lldb::SBProcess::GetScriptedImplementation;

%feature("docstring", "
    Writes the status of this process into the given `SBStream`.

    The status is what the ``process status`` command prints: the process state,
    and if it is stopped the reason and location where it stopped."
) lldb::SBProcess::GetStatus;
