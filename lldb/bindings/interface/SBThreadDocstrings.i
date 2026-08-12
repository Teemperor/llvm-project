%feature("docstring",
"Represents a thread of execution of a process.

Threads belong to an `SBProcess` and are obtained from it, either by index
(`SBProcess.GetThreadAtIndex`), by identifier
(`SBProcess.GetThreadByID`) or as the currently selected thread
(`SBProcess.GetSelectedThread`).

SBThreads can be referred to by their ID, which maps to the system specific thread
identifier, or by IndexID.  The ID may or may not be unique depending on whether the
system reuses its thread identifiers.  The IndexID is a monotonically increasing identifier
that will always uniquely reference a particular thread, and when that thread goes
away it will not be reused.

A thread is mostly used for three things: inspecting its call stack
(`SBThread.GetFrameAtIndex`, `SBThread.GetNumFrames`), finding out why it
stopped (`SBThread.GetStopReason`, `SBThread.GetStopDescription`) and stepping
it (`SBThread.StepOver`, `SBThread.StepInto`, `SBThread.StepOut`).

In Python an SBThread is iterable and yields its `SBFrame` objects from the
innermost frame outwards::

    for thread in process:
        if thread.GetStopReason() == lldb.eStopReasonBreakpoint:
            print('thread %d stopped at a breakpoint:' % thread.GetThreadID())
            for frame in thread:
                print('  %s' % frame.GetDisplayFunctionName())

Note that LLDB is process centric: when one thread stops, the whole process
stops, and stepping one thread lets the other threads run unless they are
suspended (see `SBThread.Suspend`).

See also :py:class:`SBFrame` and :py:class:`SBProcess`."
) lldb::SBThread;

%feature("docstring", "
    Returns the name of the broadcaster class that sends thread events.

    Use this with `SBListener.StartListeningForEventClass` to listen for thread
    events such as a thread being selected or a stack frame changing."
) lldb::SBThread::GetBroadcasterClassName;

%feature("docstring", "
    Returns whether this object refers to a thread.

    A thread becomes invalid when it exits or when the process it belongs to
    exits, so threads should be re-fetched from the process after every stop."
) lldb::SBThread::IsValid;

%feature("docstring", "
    Resets this object to an invalid thread."
) lldb::SBThread::Clear;

%feature("docstring", "
    Returns why this thread stopped as one of the ``lldb.eStopReason*``
    enumerators.

    Common values are ``lldb.eStopReasonBreakpoint``,
    ``lldb.eStopReasonWatchpoint``, ``lldb.eStopReasonPlanComplete`` (a step
    finished), ``lldb.eStopReasonSignal`` and ``lldb.eStopReasonNone`` for
    threads that were not the reason the process stopped::

        if thread.GetStopReason() == lldb.eStopReasonBreakpoint:
            # The stop reason data holds the breakpoint and location ID.
            bp_id = thread.GetStopReasonDataAtIndex(0)

    See `SBThread.GetStopReasonDataAtIndex` for the data that belongs to a stop
    reason and `SBThread.GetStopDescription` for a human readable version."
) lldb::SBThread::GetStopReason;

%feature("docstring", "
    Get the number of words associated with the stop reason.
    See also `SBThread.GetStopReasonDataAtIndex`."
) lldb::SBThread::GetStopReasonDataCount;

%feature("docstring", "
    Get information associated with a stop reason.

    Breakpoint stop reasons will have data that consists of pairs of
    breakpoint IDs followed by the breakpoint location IDs (they always come
    in pairs).

    Stop Reason              Count Data Type
    ======================== ===== =========================================
    eStopReasonNone          0
    eStopReasonTrace         0
    eStopReasonBreakpoint    N     duple: {breakpoint id, location id}
    eStopReasonWatchpoint    1     watchpoint id
    eStopReasonSignal        1     unix signal number
    eStopReasonException     N     exception data
    eStopReasonExec          0
    eStopReasonFork          1     pid of the child process
    eStopReasonVFork         1     pid of the child process
    eStopReasonVForkDone     0
    eStopReasonPlanComplete  0

    Use `SBThread.GetStopReasonDataCount` to find out how many words are
    available and `SBThread.GetStopReason` for the reason itself. For example,
    finding the breakpoint a thread stopped at::

        if thread.GetStopReason() == lldb.eStopReasonBreakpoint:
            breakpoint = target.FindBreakpointByID(thread.GetStopReasonDataAtIndex(0))
"
) lldb::SBThread::GetStopReasonDataAtIndex;

%feature("docstring", "
    Collects a thread's stop reason extended information dictionary and prints it
    into the SBStream in a JSON format. The format of this JSON dictionary depends
    on the stop reason and is currently used only for instrumentation plugins."
) lldb::SBThread::GetStopReasonExtendedInfoAsJSON;

%feature("docstring", "
    Returns a collection of historical stack traces that are significant to the
    current stop reason. Used by ThreadSanitizer, where we provide various stack
    traces that were involved in a data race or other type of detected issue.

    ``type`` is one of the ``lldb.eInstrumentationRuntimeType*`` enumerators.
    The result is an `SBThreadCollection` of history threads that can be
    inspected like normal threads but not resumed."
) lldb::SBThread::GetStopReasonExtendedBacktraces;

%feature("docstring", "
    Returns a human-readable description of why this thread stopped.

    In Python this takes the maximum length of the description and returns it as
    a string::

        print(thread.GetStopDescription(1024))

    See `SBThread.GetStopReason` for the machine readable version."
) lldb::SBThread::GetStopDescription;

%feature("docstring", "
    Returns the return value of the function this thread just stepped out of.

    Only valid right after a `SBThread.StepOut` or
    `SBThread.StepOutOfFrame` completed, and only for functions whose return
    value LLDB knows how to read. Returns an invalid `SBValue` otherwise."
) lldb::SBThread::GetStopReturnValue;

%feature("docstring", "
    Returns a unique thread identifier (type lldb::tid_t, typically a 64-bit type)
    for the current SBThread that will remain constant throughout the thread's
    lifetime in this process and will not be reused by another thread during this
    process lifetime.  On Mac OS X systems, this is a system-wide unique thread
    identifier; this identifier is also used by other tools like sample which helps
    to associate data from those tools with lldb.  See related `SBThread.GetIndexID`."
) lldb::SBThread::GetThreadID;

%feature("docstring", "
    Return the index number for this SBThread.  The index number is the same thing
    that a user gives as an argument to 'thread select' in the command line lldb.
    These numbers start at 1 (for the first thread lldb sees in a debug session)
    and increments up throughout the process lifetime.  An index number will not be
    reused for a different thread later in a process - thread 1 will always be
    associated with the same thread.  See related `SBThread.GetThreadID`.
    This method returns a uint32_t index number, takes no arguments."
) lldb::SBThread::GetIndexID;

%feature("docstring", "
    Returns the name of this thread, if it has one.

    Thread names are set by the program itself (for example with
    ``pthread_setname_np``), so this returns ``None`` for most threads."
) lldb::SBThread::GetName;

%feature("docstring", "
    Return the queue name associated with this thread, if any, as a str.
    For example, with a libdispatch (aka Grand Central Dispatch) queue."
) lldb::SBThread::GetQueueName;

%feature("docstring", "
    Return the dispatch_queue_id for this thread, if any, as a lldb::queue_id_t.
    For example, with a libdispatch (aka Grand Central Dispatch) queue."
) lldb::SBThread::GetQueueID;

%feature("docstring", "
    Takes a path string and a SBStream reference as parameters, returns a bool.
    Collects the thread's 'info' dictionary from the remote system, uses the path
    argument to descend into the dictionary to an item of interest, and prints
    it into the SBStream in a natural format.  Return bool is to indicate if
    anything was printed into the stream (true) or not (false)."
) lldb::SBThread::GetInfoItemByPathAsString;

%feature("docstring", "
    Return the SBQueue for this thread.  If this thread is not currently associated
    with a libdispatch queue, the SBQueue object's IsValid() method will return false.
    If this SBThread is actually a HistoryThread, we may be able to provide QueueID
    and QueueName, but not provide an SBQueue.  Those individual attributes may have
    been saved for the HistoryThread without enough information to reconstitute the
    entire SBQueue at that time.
    This method takes no arguments, returns an SBQueue."
) lldb::SBThread::GetQueue;

%feature("docstring",
    "Do a source level single step over in the currently selected thread.

    ``stop_other_threads`` is one of the ``lldb.eOnlyThisThread``,
    ``lldb.eAllThreads`` or ``lldb.eOnlyDuringStepping`` run modes and controls
    which threads are allowed to run while stepping.

    Like all stepping functions this resumes the process, so it should be
    called on a stopped process and any `SBFrame` obtained before the call is
    invalid afterwards::

        thread.StepOver()
        frame = thread.GetFrameAtIndex(0)  # Re-fetch the frame after stepping.

    See also `SBThread.StepInto`, `SBThread.StepOut` and
    `SBThread.StepInstruction`."
) lldb::SBThread::StepOver;

%feature("docstring", "
    Step into the function that is called at the current source line.

    If the current line contains no call, this behaves like
    `SBThread.StepOver`.

    ``target_name`` restricts the step to a specific function: stepping only
    stops in a function with that name and steps over any other call. If
    ``target_name`` is ``None`` stepping will stop in any of the places we would
    normally stop.

    ``end_line`` steps the current thread from the current source line to the
    line given by end_line, stopping if the thread steps into the function given
    by target_name."
) lldb::SBThread::StepInto;

%feature("docstring",
    "Step out of the currently selected frame.

    Runs the thread until the function of the selected frame returns. See
    `SBThread.GetStopReturnValue` to read the value the function returned and
    `SBThread.StepOutOfFrame` to step out of a specific frame."
) lldb::SBThread::StepOut;

%feature("docstring",
    "Step out of the specified frame.

    Runs the thread until the given `SBFrame` and all frames below it have
    returned."
) lldb::SBThread::StepOutOfFrame;

%feature("docstring",
    "Do an instruction level single step in the currently selected thread.

    If ``step_over`` is ``True`` a call instruction is stepped over instead of
    stepped into. Unlike `SBThread.StepOver` this does not use line table
    information, so it stops after exactly one machine instruction."
) lldb::SBThread::StepInstruction;

%feature("docstring", "
    Steps the thread until it reaches the given line of the given file.

    Steps over calls, so this is like repeatedly calling `SBThread.StepOver`
    until the given line in the given `SBFileSpec` is reached. Returns an
    `SBError` if the line has no code associated with it or if it cannot be
    reached from ``frame``::

        error = thread.StepOverUntil(frame, lldb.SBFileSpec('main.c'), 42)
"
) lldb::SBThread::StepOverUntil;

%feature("docstring", "
    Steps this thread using a thread plan implemented in Python.

    ``script_class_name`` is the name of a Python class deriving from
    ``lldb.ScriptedThreadPlan`` that implements the stepping logic, see
    :doc:`/use/python-reference`. ``args_data`` is an `SBStructuredData` that is
    passed to the plan's constructor, and ``resume_immediately`` controls
    whether the process is resumed right away or the plan is only queued.

    Returns an `SBError` describing any problem with the plan."
) lldb::SBThread::StepUsingScriptedThreadPlan;

%feature("docstring", "
    Sets the program counter of this thread to the given line of the given file.

    This changes where the thread will continue executing without running any of
    the code in between, which can easily corrupt the state of the program.
    Returns an `SBError` if there is no code for that line in the current
    function."
) lldb::SBThread::JumpToLine;

%feature("docstring", "
    Runs this thread until it reaches the given load address.

    This is like setting a temporary breakpoint at ``addr`` and continuing,
    except that only this thread is allowed to run. Note that if the address is
    never reached the process will not stop again on its own."
) lldb::SBThread::RunToAddress;

%feature("docstring", "
    Force a return from the frame passed in (and any frames younger than it)
    without executing any more code in those frames.  If return_value contains
    a valid SBValue, that will be set as the return value from frame.  Note, at
    present only scalar return values are supported.

    This is what the ``thread return`` command does."
) lldb::SBThread::ReturnFromFrame;

%feature("docstring", "
    Unwind the stack frames from the innermost expression evaluation.
    This API is equivalent to 'thread return -x'."
) lldb::SBThread::UnwindInnermostExpression;

%feature("docstring", "
    LLDB currently supports process centric debugging which means when any
    thread in a process stops, all other threads are stopped. The Suspend()
    call here tells our process to suspend a thread and not let it run when
    the other threads in a process are allowed to run. So when
    `SBProcess.Continue` is called, any threads that aren't suspended will
    be allowed to run. If any of the SBThread functions for stepping are
    called (StepOver, StepInto, StepOut, StepInstruction, RunToAddress), the
    thread will now be allowed to run and these functions will simply return.

    Eventually we plan to add support for thread centric debugging where
    each thread is controlled individually and each thread would broadcast
    its state, but we haven't implemented this yet.

    Likewise the `SBThread.Resume` call will again allow the thread to run
    when the process is continued.

    Suspend() and Resume() functions are not currently reference counted, if
    anyone has the need for them to be reference counted, please let us
    know."
) lldb::SBThread::Suspend;

%feature("docstring", "
    Allows this thread to run again when the process is continued.

    This undoes a previous `SBThread.Suspend`. Returns ``False`` if the thread
    could not be resumed, in which case the `SBError` overload explains why."
) lldb::SBThread::Resume;

%feature("docstring", "
    Returns whether this thread is suspended, see `SBThread.Suspend`.

    Note that this does not describe whether the thread is currently running:
    it reports whether the thread will be held back the next time the process
    is continued."
) lldb::SBThread::IsSuspended;

%feature("docstring", "
    Returns whether this thread is stopped.

    Since LLDB stops all threads of a process at once this is usually the same
    for all threads of a process, see `SBProcess.GetState`."
) lldb::SBThread::IsStopped;

%feature("docstring", "
    Returns the number of stack frames of this thread.

    Computing this requires unwinding the whole stack, which can be expensive
    for deep stacks. Frame ``0`` is the innermost frame."
) lldb::SBThread::GetNumFrames;

%feature("docstring", "
    Returns the stack frame at the given index as an `SBFrame`.

    Frame ``0`` is the innermost (currently executing) frame, frame ``1`` its
    caller and so on. Returns an invalid frame if the index is out of range::

        frame = thread.GetFrameAtIndex(0)

    In Python, iterating over a thread yields all of its frames."
) lldb::SBThread::GetFrameAtIndex;

%feature("docstring", "
    Returns all stack frames of this thread as an `SBFrameList`.

    Unlike repeatedly calling `SBThread.GetFrameAtIndex`, the frames are
    fetched as a batch, which can be considerably faster."
) lldb::SBThread::GetFrames;

%feature("docstring", "
    Returns the frame that is currently selected in this thread.

    The selected frame is the one commands such as ``frame variable`` operate
    on. It defaults to frame ``0`` or to the frame a frame recognizer chose."
) lldb::SBThread::GetSelectedFrame;

%feature("docstring", "
    Selects the frame with the given index and returns it.

    The selection is what the ``frame select`` command changes and what
    `SBThread.GetSelectedFrame` returns afterwards."
) lldb::SBThread::SetSelectedFrame;

%feature("docstring", "
    Returns whether the given `SBEvent` is a thread event.

    Thread events are broadcast by the broadcaster class returned by
    `SBThread.GetBroadcasterClassName`."
) lldb::SBThread::EventIsThreadEvent;

%feature("docstring", "
    Returns the `SBFrame` a thread event refers to.

    Only meaningful for stack-frame-changed events, see
    `SBThread.EventIsThreadEvent`."
) lldb::SBThread::GetStackFrameFromEvent;

%feature("docstring", "
    Returns the `SBThread` a thread event refers to.

    See `SBThread.EventIsThreadEvent`."
) lldb::SBThread::GetThreadFromEvent;

%feature("docstring", "
    Returns the `SBProcess` this thread belongs to."
) lldb::SBThread::GetProcess;

%feature("docstring", "
    Get the description strings for this thread that match what the
    lldb driver will present, using the thread-format (stop_format==false)
    or thread-stop-format (stop_format = true).

    See `SBThread.GetDescriptionWithFormat` to use a custom format string."
) lldb::SBThread::GetDescription;

%feature("docstring", "
    Writes a description of this thread into the given `SBStream`, using a
    custom format.

    ``format`` is an `SBFormat` created from a format string as described in
    https://lldb.llvm.org/use/formatting.html. Returns an `SBError` describing
    any problem with the format string."
) lldb::SBThread::GetDescriptionWithFormat;

%feature("docstring", "
    Writes the status of this thread into the given `SBStream`.

    The status is what the ``thread status`` command prints: the stop reason
    followed by the current frame and the source line it is stopped at."
) lldb::SBThread::GetStatus;

%feature("docstring","
    Given an argument of str to specify the type of thread-origin extended
    backtrace to retrieve, query whether the origin of this thread is
    available.  An SBThread is retured; SBThread.IsValid will return true
    if an extended backtrace was available.  The returned SBThread is not
    a part of the SBProcess' thread list and it cannot be manipulated like
    normal threads -- you cannot step or resume it, for instance -- it is
    intended to used primarily for generating a backtrace.  You may request
    the returned thread's own thread origin in turn."
) lldb::SBThread::GetExtendedBacktraceThread;

%feature("docstring","
    If this SBThread is an ExtendedBacktrace thread, get the IndexID of the
    original thread that this ExtendedBacktrace thread represents, if
    available.  The thread that was running this backtrace in the past may
    not have been registered with lldb's thread index (if it was created,
    did its work, and was destroyed without lldb ever stopping execution).
    In that case, this ExtendedBacktrace thread's IndexID will be returned."
) lldb::SBThread::GetExtendedBacktraceOriginatingIndexID;

%feature("docstring","
    Returns an SBValue object represeting the current exception for the thread,
    if there is any. Currently, this works for Obj-C code and returns an SBValue
    representing the NSException object at the throw site or that's currently
    being processes."
) lldb::SBThread::GetCurrentException;

%feature("docstring","
    Returns a historical (fake) SBThread representing the stack trace of an
    exception, if there is one for the thread. Currently, this works for Obj-C
    code, and can retrieve the throw-site backtrace of an NSException object
    even when the program is no longer at the throw site."
) lldb::SBThread::GetCurrentExceptionBacktrace;

%feature("docstring","
    lldb may be able to detect that function calls should not be executed
    on a given thread at a particular point in time.  It is recommended that
    this is checked before performing an inferior function call on a given
    thread."
) lldb::SBThread::SafeToCallFunctions;

%feature("docstring","
    Returns a SBValue object representing the siginfo for the current signal.
    "
) lldb::SBThread::GetSiginfo;
