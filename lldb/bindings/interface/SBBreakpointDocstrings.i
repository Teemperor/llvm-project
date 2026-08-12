%feature("docstring",
"Represents a logical breakpoint and its associated settings.

A breakpoint describes *where* the program should stop, and it is created from
an `SBTarget` with one of its ``BreakpointCreate*`` functions, e.g.
`SBTarget.BreakpointCreateByName` or
`SBTarget.BreakpointCreateByLocation`::

    breakpoint = target.BreakpointCreateByName('main', 'a.out')

One breakpoint can resolve to any number of addresses in the program, and each
of those is an `SBBreakpointLocation`. For example a breakpoint on a C++
template function or on an inlined function usually has several locations, and a
breakpoint on a function of a library that isn't loaded yet has none until the
library shows up. `SBBreakpoint.GetNumLocations` reports how many there are.

Beyond the location, a breakpoint has options that decide whether stopping
actually happens: a condition (`SBBreakpoint.SetCondition`), an ignore count
(`SBBreakpoint.SetIgnoreCount`), a thread restriction
(`SBBreakpoint.SetThreadID`), a callback
(`SBBreakpoint.SetScriptCallbackFunction`) and commands to run
(`SBBreakpoint.SetCommandLineCommands`). For example, stopping only the third
time a function is called on a specific thread::

    breakpoint.SetIgnoreCount(2)
    breakpoint.SetThreadID(thread.GetThreadID())

Breakpoints can also be grouped by name (`SBBreakpoint.AddNameWithErrorHandling`
and `SBBreakpointName`), which makes it possible to configure many of them at
once.

SBBreakpoint supports breakpoint location iteration, for example,::

    for bl in breakpoint:
        print('breakpoint location load addr: %s' % hex(bl.GetLoadAddress()))
        print('breakpoint location condition: %s' % bl.GetCondition())

and rich comparison methods which allow the API program to use,::

    if aBreakpoint == bBreakpoint:
        ...

to compare two breakpoints for equality.

See also :py:class:`SBBreakpointLocation`, :py:class:`SBBreakpointName` and
:py:class:`SBTarget`."
) lldb::SBBreakpoint;

%feature("docstring", "
    Returns the ID of this breakpoint.

    This is the number the ``breakpoint list`` command shows and that
    `SBTarget.FindBreakpointByID` takes."
) lldb::SBBreakpoint::GetID;

%feature("docstring", "
    Returns whether this object refers to a breakpoint.

    A breakpoint becomes invalid when it is deleted from its target."
) lldb::SBBreakpoint::IsValid;

%feature("docstring", "
    Removes the breakpoint traps from the process without deleting the
    breakpoint.

    The breakpoint stays in the target's breakpoint list and its locations are
    re-inserted the next time the process resumes."
) lldb::SBBreakpoint::ClearAllBreakpointSites;

%feature("docstring", "
    Returns the `SBTarget` this breakpoint belongs to."
) lldb::SBBreakpoint::GetTarget;

%feature("docstring", "
    Returns the location of this breakpoint at the given load address.

    Returns an invalid `SBBreakpointLocation` if this breakpoint has no location
    at that address."
) lldb::SBBreakpoint::FindLocationByAddress;

%feature("docstring", "
    Returns the ID of the location of this breakpoint at the given load
    address.

    Returns ``lldb.LLDB_INVALID_BREAK_ID`` if there is no such location. The ID
    can be passed to `SBBreakpoint.FindLocationByID`."
) lldb::SBBreakpoint::FindLocationIDByAddress;

%feature("docstring", "
    Returns the location of this breakpoint with the given ID.

    Location IDs are the numbers after the dot in the breakpoint IDs the
    ``breakpoint list`` command prints, e.g. ``2`` in ``1.2``."
) lldb::SBBreakpoint::FindLocationByID;

%feature("docstring", "
    Returns the location at the given index as an `SBBreakpointLocation`.

    See `SBBreakpoint.GetNumLocations`; in Python, iterating over a breakpoint
    yields all of its locations."
) lldb::SBBreakpoint::GetLocationAtIndex;

%feature("docstring", "
    Enables or disables this breakpoint.

    A disabled breakpoint keeps all its settings and locations but does not stop
    the process."
) lldb::SBBreakpoint::SetEnabled;

%feature("docstring", "
    Returns whether this breakpoint is enabled, see
    `SBBreakpoint.SetEnabled`."
) lldb::SBBreakpoint::IsEnabled;

%feature("docstring", "
    Sets whether this breakpoint is deleted after it is hit once."
) lldb::SBBreakpoint::SetOneShot;

%feature("docstring", "
    Returns whether this breakpoint is a one-shot breakpoint, see
    `SBBreakpoint.SetOneShot`."
) lldb::SBBreakpoint::IsOneShot;

%feature("docstring", "
    Returns whether this breakpoint is internal to LLDB.

    Internal breakpoints are used by LLDB itself, for example by the dynamic
    loader or by ``thread step-over``, and are not shown to users."
) lldb::SBBreakpoint::IsInternal;

%feature("docstring", "
    Returns how often this breakpoint was hit.

    Hits that were ignored because of the ignore count or a false condition are
    included in this count."
) lldb::SBBreakpoint::GetHitCount;

%feature("docstring", "
    Sets how many hits of this breakpoint are ignored before it stops.

    The count is decremented on every hit, so setting it to ``2`` means the
    third hit stops the process."
) lldb::SBBreakpoint::SetIgnoreCount;

%feature("docstring", "
    Returns how many more hits of this breakpoint are ignored, see
    `SBBreakpoint.SetIgnoreCount`."
) lldb::SBBreakpoint::GetIgnoreCount;

%feature("docstring", "
    The breakpoint stops only if the condition expression evaluates to true.

    The condition is source code in the language of the breakpoint's location and
    is evaluated in the frame that hit the breakpoint::

        breakpoint.SetCondition('argc > 1')

    Note that evaluating a condition on every hit is expensive; a breakpoint
    with a condition can slow down the program considerably."
) lldb::SBBreakpoint::SetCondition;

%feature("docstring", "
    Get the condition expression for the breakpoint."
) lldb::SBBreakpoint::GetCondition;

%feature("docstring", "
    Sets whether the process continues automatically after this breakpoint was
    hit.

    Together with a callback or commands this makes it possible to trace a
    program without stopping it: the breakpoint gathers information and
    execution continues."
) lldb::SBBreakpoint::SetAutoContinue;

%feature("docstring", "
    Returns whether the process continues automatically after a hit, see
    `SBBreakpoint.SetAutoContinue`."
) lldb::SBBreakpoint::GetAutoContinue;

%feature("docstring", "
    Restricts this breakpoint to the thread with the given thread ID.

    Other threads don't stop when they hit the breakpoint. See
    `SBThread.GetThreadID` and `SBBreakpoint.SetThreadIndex`."
) lldb::SBBreakpoint::SetThreadID;

%feature("docstring", "
    Returns the thread ID this breakpoint is restricted to, see
    `SBBreakpoint.SetThreadID`."
) lldb::SBBreakpoint::GetThreadID;

%feature("docstring", "
    Restricts this breakpoint to the thread with the given index ID.

    See `SBThread.GetIndexID`."
) lldb::SBBreakpoint::SetThreadIndex;

%feature("docstring", "
    Returns the thread index this breakpoint is restricted to, see
    `SBBreakpoint.SetThreadIndex`."
) lldb::SBBreakpoint::GetThreadIndex;

%feature("docstring", "
    Restricts this breakpoint to threads with the given name.

    See `SBThread.GetName`."
) lldb::SBBreakpoint::SetThreadName;

%feature("docstring", "
    Returns the thread name this breakpoint is restricted to, see
    `SBBreakpoint.SetThreadName`."
) lldb::SBBreakpoint::GetThreadName;

%feature("docstring", "
    Restricts this breakpoint to threads running on the given libdispatch
    queue.

    See `SBThread.GetQueueName`."
) lldb::SBBreakpoint::SetQueueName;

%feature("docstring", "
    Returns the queue name this breakpoint is restricted to, see
    `SBBreakpoint.SetQueueName`."
) lldb::SBBreakpoint::GetQueueName;

%feature("docstring", "
    Set the name of the script function to be called when the breakpoint is hit.

    The function takes ``(frame, bp_loc, internal_dict)``. Returning ``False``
    from it makes the process continue as if the breakpoint had not been hit::

        breakpoint.SetScriptCallbackFunction('my_module.my_callback')

    To use the variant that takes ``extra_args``, the function should take
    ``(frame, bp_loc, extra_args, internal_dict)`` and
    when the breakpoint is hit the extra_args will be passed to the callback
    function. This makes it possible to reuse one callback with different
    parameters, which are passed as an `SBStructuredData`."
) lldb::SBBreakpoint::SetScriptCallbackFunction;

%feature("docstring", "
    Provide the body for the script function to be called when the breakpoint is hit.
    The body will be wrapped in a function, which be passed two arguments:
    'frame' - which holds the bottom-most SBFrame of the thread that hit the breakpoint
    'bpno'  - which is the SBBreakpointLocation to which the callback was attached.

    The error parameter is currently ignored, but will at some point hold the Python
    compilation diagnostics.
    Returns true if the body compiles successfully, false if not.

    For example,::

        breakpoint.SetScriptCallbackBody('print(frame.GetFunctionName())')
"
) lldb::SBBreakpoint::SetScriptCallbackBody;

%feature("docstring", "
    Sets the LLDB command line commands that are run when this breakpoint is
    hit.

    ``commands`` is an `SBStringList` with one command per entry, just like the
    commands of the ``breakpoint command add`` command::

        commands = lldb.SBStringList()
        commands.AppendString('bt')
        commands.AppendString('frame variable')
        breakpoint.SetCommandLineCommands(commands)
"
) lldb::SBBreakpoint::SetCommandLineCommands;

%feature("docstring", "
    Fills the given `SBStringList` with the commands of this breakpoint.

    Returns whether this breakpoint has any commands, see
    `SBBreakpoint.SetCommandLineCommands`."
) lldb::SBBreakpoint::GetCommandLineCommands;

%feature("docstring", "
    Deprecated, use `SBBreakpoint.AddNameWithErrorHandling`."
) lldb::SBBreakpoint::AddName;

%feature("docstring", "
    Adds a name to this breakpoint and returns an `SBError`.

    Names are a way to group breakpoints; they can be used in the command line
    interface wherever a breakpoint ID is expected and their options are shared,
    see `SBBreakpointName`::

        error = breakpoint.AddNameWithErrorHandling('my-breakpoints')
"
) lldb::SBBreakpoint::AddNameWithErrorHandling;

%feature("docstring", "
    Removes a name from this breakpoint, see
    `SBBreakpoint.AddNameWithErrorHandling`."
) lldb::SBBreakpoint::RemoveName;

%feature("docstring", "
    Returns whether this breakpoint has the given name."
) lldb::SBBreakpoint::MatchesName;

%feature("docstring", "
    Fills the given `SBStringList` with all names of this breakpoint."
) lldb::SBBreakpoint::GetNames;

%feature("docstring", "
    Returns how many locations of this breakpoint are resolved.

    A location is resolved once LLDB knows the address it belongs to, which for
    code in shared libraries only happens after the library was loaded. See
    `SBBreakpointLocation.IsResolved`."
) lldb::SBBreakpoint::GetNumResolvedLocations;

%feature("docstring", "
    Returns how many locations this breakpoint has.

    A breakpoint with zero locations does not stop the program; that usually
    means the location it describes was not found or is not loaded yet."
) lldb::SBBreakpoint::GetNumLocations;

%feature("docstring", "
    Writes a description of this breakpoint into the given `SBStream`.

    If ``include_locations`` is ``True`` the description also lists all
    locations of the breakpoint."
) lldb::SBBreakpoint::GetDescription;

%feature("docstring", "
    Returns whether the given `SBEvent` is a breakpoint event.

    Breakpoint events are sent when breakpoints are added, removed or their
    locations change. See `SBTarget.GetBroadcaster`."
) lldb::SBBreakpoint::EventIsBreakpointEvent;

%feature("docstring", "
    Returns what happened to the breakpoint of a breakpoint event.

    The result is one of the ``lldb.eBreakpointEvent*`` enumerators, e.g.
    ``lldb.eBreakpointEventTypeLocationsAdded``."
) lldb::SBBreakpoint::GetBreakpointEventTypeFromEvent;

%feature("docstring", "
    Returns the `SBBreakpoint` a breakpoint event refers to."
) lldb::SBBreakpoint::GetBreakpointFromEvent;

%feature("docstring", "
    Returns one of the locations a breakpoint event refers to.

    See `SBBreakpoint.GetNumBreakpointLocationsFromEvent`."
) lldb::SBBreakpoint::GetBreakpointLocationAtIndexFromEvent;

%feature("docstring", "
    Returns how many locations a breakpoint event refers to."
) lldb::SBBreakpoint::GetNumBreakpointLocationsFromEvent;

%feature("docstring", "
    Returns whether this is a hardware breakpoint.

    Hardware breakpoints don't modify the program's code, which is necessary for
    code in read-only memory such as ROM, but the number of them is limited by
    the hardware."
) lldb::SBBreakpoint::IsHardware;

%feature("docstring", "
    Makes this breakpoint a hardware breakpoint.

    This replaces all existing breakpoint locations with hardware breakpoints.
    Returns an `SBError` if this fails, e.g. when there aren't enough hardware
    resources available."
) lldb::SBBreakpoint::SetIsHardware;

%feature("docstring", "
    Adds a location to this breakpoint at the given `SBAddress`.

    Can only be called from the ``__callback__`` method of a scripted breakpoint
    resolver, see `SBTarget.BreakpointCreateFromScript`."
) lldb::SBBreakpoint::AddLocation;

%feature("docstring", "
    Adds a facade location to this breakpoint.

    A facade location is a location that is not backed by an address in the
    target; the scripted resolver decides what it looks like and when it counts
    as hit. Returns the location that was added, which can be used in the
    resolver's ``get_location_description`` and ``was_hit`` methods. Can only be
    called from a scripted breakpoint resolver, see
    `SBTarget.BreakpointCreateFromScript`."
) lldb::SBBreakpoint::AddFacadeLocation;

%feature("docstring", "
    Returns this breakpoint's settings as `SBStructuredData`.

    This is the same representation that
    `SBTarget.BreakpointsWriteToFile` writes, so it can be used to inspect or
    store a breakpoint."
) lldb::SBBreakpoint::SerializeToStructuredData;

%feature("docstring",
"Represents a list of :py:class:`SBBreakpoint`.

Breakpoint lists are used by the functions that operate on several breakpoints
at once, such as `SBTarget.FindBreakpointsByName` and
`SBTarget.BreakpointsWriteToFile`. In Python they support ``len()``, indexing
and iteration::

    breakpoints = lldb.SBBreakpointList(target)
    target.FindBreakpointsByName('my-breakpoints', breakpoints)
    for breakpoint in breakpoints:
        print(breakpoint.GetID())
"
) lldb::SBBreakpointList;

%feature("docstring",
"Returns the number of breakpoints in this list.

In Python this is also what ``len()`` returns."
) lldb::SBBreakpointList::GetSize;

%feature("docstring",
"Returns the breakpoint at the given index as an `SBBreakpoint`."
) lldb::SBBreakpointList::GetBreakpointAtIndex;

%feature("docstring",
"Returns the breakpoint in this list with the given ID.

Returns an invalid `SBBreakpoint` if the list has no such breakpoint."
) lldb::SBBreakpointList::FindBreakpointByID;

%feature("docstring",
"Appends a breakpoint to this list."
) lldb::SBBreakpointList::Append;

%feature("docstring",
"Appends a breakpoint to this list if it isn't in it yet.

Returns whether the breakpoint was appended."
) lldb::SBBreakpointList::AppendIfUnique;

%feature("docstring",
"Appends the breakpoint with the given ID to this list."
) lldb::SBBreakpointList::AppendByID;

%feature("docstring",
"Removes all breakpoints from this list.

The breakpoints themselves are not deleted, use
`SBTarget.BreakpointDelete` for that."
) lldb::SBBreakpointList::Clear;
