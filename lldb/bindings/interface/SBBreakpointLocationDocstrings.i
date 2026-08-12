%feature("docstring",
"Represents one unique instance (by address) of a logical breakpoint.

A breakpoint location is defined by the breakpoint that produces it,
and the address that resulted in this particular instantiation.
Each breakpoint location has its settable options.

:py:class:`SBBreakpoint` contains SBBreakpointLocation(s). They are obtained
with `SBBreakpoint.GetLocationAtIndex` or by iterating over a breakpoint in
Python::

    for location in breakpoint:
        print('%s at %s' % (location.GetID(), location.GetAddress()))

A location has the same options as the breakpoint it belongs to (condition,
ignore count, thread restriction, callbacks), but setting them on the location
only affects that one address. This is how a single breakpoint can, for example,
be made to stop only in one of the many instantiations of a template
function."
) lldb::SBBreakpointLocation;

%feature("docstring", "
    Returns the ID of this location within its breakpoint.

    Together with the breakpoint ID this forms the ``1.2``-style identifier the
    ``breakpoint list`` command shows. See
    `SBBreakpoint.FindLocationByID`."
) lldb::SBBreakpointLocation::GetID;

%feature("docstring", "
    Returns whether this object refers to a breakpoint location."
) lldb::SBBreakpointLocation::IsValid;

%feature("docstring", "
    Returns the address of this location as an `SBAddress`.

    The address can be resolved to a symbol, function or line entry, see
    `SBAddress.GetSymbolContext`."
) lldb::SBBreakpointLocation::GetAddress;

%feature("docstring", "
    Returns the load address of this location as an integer.

    Returns ``lldb.LLDB_INVALID_ADDRESS`` if the location is not resolved yet,
    see `SBBreakpointLocation.IsResolved`."
) lldb::SBBreakpointLocation::GetLoadAddress;

%feature("docstring", "
    Enables or disables this location.

    Only this address stops being a breakpoint; the other locations of the
    breakpoint are unaffected."
) lldb::SBBreakpointLocation::SetEnabled;

%feature("docstring", "
    Returns whether this location is enabled, see
    `SBBreakpointLocation.SetEnabled`."
) lldb::SBBreakpointLocation::IsEnabled;

%feature("docstring", "
    Returns how often this location was hit."
) lldb::SBBreakpointLocation::GetHitCount;

%feature("docstring", "
    Returns how many more hits of this location are ignored, see
    `SBBreakpointLocation.SetIgnoreCount`."
) lldb::SBBreakpointLocation::GetIgnoreCount;

%feature("docstring", "
    Sets how many hits of this location are ignored before it stops.

    See `SBBreakpoint.SetIgnoreCount`, which does the same for all locations of
    a breakpoint."
) lldb::SBBreakpointLocation::SetIgnoreCount;

%feature("docstring", "
    The breakpoint location stops only if the condition expression evaluates
    to true.

    See `SBBreakpoint.SetCondition` for details; setting the condition here only
    affects this one address.") lldb::SBBreakpointLocation::SetCondition;

%feature("docstring", "
    Get the condition expression for the breakpoint location."
) lldb::SBBreakpointLocation::GetCondition;

%feature("docstring", "
    Sets whether the process continues automatically after this location was
    hit.

    See `SBBreakpoint.SetAutoContinue`."
) lldb::SBBreakpointLocation::SetAutoContinue;

%feature("docstring", "
    Returns whether the process continues automatically after a hit, see
    `SBBreakpointLocation.SetAutoContinue`."
) lldb::SBBreakpointLocation::GetAutoContinue;

%feature("docstring", "
    Set the callback to the given Python function name.
    The function takes three arguments (frame, bp_loc, internal_dict).

    Returning ``False`` from the callback makes the process continue as if the
    location had not been hit. The variant that takes ``extra_args`` expects a
    function taking (frame, bp_loc, extra_args, internal_dict) and
    when the breakpoint is hit the extra_args will be passed to the callback
    function. See `SBBreakpoint.SetScriptCallbackFunction`."
) lldb::SBBreakpointLocation::SetScriptCallbackFunction;

%feature("docstring", "
    Provide the body for the script function to be called when the breakpoint location is hit.
    The body will be wrapped in a function, which be passed two arguments:
    'frame' - which holds the bottom-most SBFrame of the thread that hit the breakpoint
    'bpno'  - which is the SBBreakpointLocation to which the callback was attached.

    The error parameter is currently ignored, but will at some point hold the Python
    compilation diagnostics.
    Returns true if the body compiles successfully, false if not."
) lldb::SBBreakpointLocation::SetScriptCallbackBody;

%feature("docstring", "
    Sets the LLDB command line commands that are run when this location is hit.

    See `SBBreakpoint.SetCommandLineCommands`."
) lldb::SBBreakpointLocation::SetCommandLineCommands;

%feature("docstring", "
    Fills the given `SBStringList` with the commands of this location.

    Returns whether this location has any commands."
) lldb::SBBreakpointLocation::GetCommandLineCommands;

%feature("docstring", "
    Restricts this location to the thread with the given thread ID.

    See `SBBreakpoint.SetThreadID`."
) lldb::SBBreakpointLocation::SetThreadID;

%feature("docstring", "
    Returns the thread ID this location is restricted to."
) lldb::SBBreakpointLocation::GetThreadID;

%feature("docstring", "
    Restricts this location to the thread with the given index ID.

    See `SBThread.GetIndexID`."
) lldb::SBBreakpointLocation::SetThreadIndex;

%feature("docstring", "
    Returns the thread index this location is restricted to."
) lldb::SBBreakpointLocation::GetThreadIndex;

%feature("docstring", "
    Restricts this location to threads with the given name."
) lldb::SBBreakpointLocation::SetThreadName;

%feature("docstring", "
    Returns the thread name this location is restricted to."
) lldb::SBBreakpointLocation::GetThreadName;

%feature("docstring", "
    Restricts this location to threads running on the given libdispatch
    queue."
) lldb::SBBreakpointLocation::SetQueueName;

%feature("docstring", "
    Returns the queue name this location is restricted to."
) lldb::SBBreakpointLocation::GetQueueName;

%feature("docstring", "
    Returns whether this location is resolved to an address in the target.

    Locations in shared libraries that are not loaded yet are unresolved and
    become resolved once the library shows up. See
    `SBBreakpoint.GetNumResolvedLocations`."
) lldb::SBBreakpointLocation::IsResolved;

%feature("docstring", "
    Writes a description of this location into the given `SBStream`.

    ``level`` is one of the ``lldb.eDescriptionLevel*`` enumerators."
) lldb::SBBreakpointLocation::GetDescription;

%feature("docstring", "
    Returns the `SBBreakpoint` this location belongs to."
) lldb::SBBreakpointLocation::GetBreakpoint;
