%feature("docstring",
"Represents a watchpoint: a breakpoint that triggers on a memory access.

A watchpoint is determined by the address and the byte size that resulted in
this particular instantiation.  Each watchpoint has its settable options.

Watchpoints are created from a variable with `SBValue.Watch` or from an address
with `SBTarget.WatchpointCreateByAddress`::

    error = lldb.SBError()
    watchpoint = frame.FindVariable('global_counter').Watch(True, False, True, error)

They usually rely on hardware support, so both the number of watchpoints and the
size of the watched region are limited, see
`SBProcess.GetNumSupportedHardwareWatchpoints`. A thread that stops because of a
watchpoint reports ``lldb.eStopReasonWatchpoint``, and the ID of the watchpoint
is available through `SBThread.GetStopReasonDataAtIndex`.

Like breakpoints, watchpoints support conditions
(`SBWatchpoint.SetCondition`) and ignore counts
(`SBWatchpoint.SetIgnoreCount`).

See also :py:class:`SBTarget.watchpoint_iter()` for example usage of iterating through the
watchpoints of the target and :py:class:`SBWatchpointOptions` for the options
that can be given when creating one."
) lldb::SBWatchpoint;

%feature("docstring", "
    Returns whether this object refers to a watchpoint."
) lldb::SBWatchpoint::IsValid;

%feature("docstring", "
    Returns an `SBError` describing why this watchpoint could not be set.

    The functions that create watchpoints return an invalid watchpoint on
    failure; this explains what went wrong, for example that no hardware
    watchpoint resources were available."
) lldb::SBWatchpoint::GetError;

%feature("docstring", "
    Returns the ID of this watchpoint.

    This is the number the ``watchpoint list`` command shows and what
    `SBTarget.FindWatchpointByID` takes."
) lldb::SBWatchpoint::GetID;

%feature("docstring", "
    Deprecated.  Previously: Return the hardware index of the
    watchpoint register.  Now: -1 is always returned."
) lldb::SBWatchpoint::GetHardwareIndex;

%feature("docstring", "
    Returns the address of the memory this watchpoint watches."
) lldb::SBWatchpoint::GetWatchAddress;

%feature("docstring", "
    Returns the size in bytes of the memory this watchpoint watches."
) lldb::SBWatchpoint::GetWatchSize;

%feature("docstring", "
    Enables or disables this watchpoint.

    A disabled watchpoint keeps its settings but does not stop the process."
) lldb::SBWatchpoint::SetEnabled;

%feature("docstring", "
    Returns whether this watchpoint is enabled, see
    `SBWatchpoint.SetEnabled`."
) lldb::SBWatchpoint::IsEnabled;

%feature("docstring", "
    Returns how often this watchpoint was hit."
) lldb::SBWatchpoint::GetHitCount;

%feature("docstring", "
    Returns how many more hits of this watchpoint are ignored, see
    `SBWatchpoint.SetIgnoreCount`."
) lldb::SBWatchpoint::GetIgnoreCount;

%feature("docstring", "
    Sets how many hits of this watchpoint are ignored before it stops."
) lldb::SBWatchpoint::SetIgnoreCount;

%feature("docstring", "
    Get the condition expression for the watchpoint."
) lldb::SBWatchpoint::GetCondition;

%feature("docstring", "
    The watchpoint stops only if the condition expression evaluates to true.

    The condition is evaluated in the frame that triggered the watchpoint, see
    `SBBreakpoint.SetCondition`."
) lldb::SBWatchpoint::SetCondition;

%feature("docstring", "
    Writes a description of this watchpoint into the given `SBStream`.

    ``level`` is one of the ``lldb.eDescriptionLevel*`` enumerators."
) lldb::SBWatchpoint::GetDescription;

%feature("docstring", "
    Resets this object to an invalid watchpoint."
) lldb::SBWatchpoint::Clear;

%feature("docstring", "
    Returns whether the given `SBEvent` is a watchpoint event."
) lldb::SBWatchpoint::EventIsWatchpointEvent;

%feature("docstring", "
    Returns what happened to the watchpoint of a watchpoint event.

    The result is one of the ``lldb.eWatchpointEvent*`` enumerators."
) lldb::SBWatchpoint::GetWatchpointEventTypeFromEvent;

%feature("docstring", "
    Returns the `SBWatchpoint` a watchpoint event refers to."
) lldb::SBWatchpoint::GetWatchpointFromEvent;

%feature("docstring", "
    Returns the type recorded when the watchpoint was created. For variable
    watchpoints it is the type of the watched variable. For expression
    watchpoints it is the type of the provided expression."
) lldb::SBWatchpoint::GetType;

%feature("docstring", "
    Returns the kind of value that was watched when the watchpoint was created.
    Returns one of the following eWatchPointValueKindVariable,
    eWatchPointValueKindExpression, eWatchPointValueKindInvalid.
    "
) lldb::SBWatchpoint::GetWatchValueKind;

%feature("docstring", "
    Get the spec for the watchpoint. For variable watchpoints this is the name
    of the variable. For expression watchpoints it is empty
    (may change in the future)."
) lldb::SBWatchpoint::GetWatchSpec;

%feature("docstring", "
    Returns true if the watchpoint is watching reads. Returns false otherwise."
) lldb::SBWatchpoint::IsWatchingReads;

%feature("docstring", "
    Returns true if the watchpoint is watching writes. Returns false otherwise."
) lldb::SBWatchpoint::IsWatchingWrites;
