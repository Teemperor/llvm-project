%feature("docstring",
"Represents a breakpoint name registered in a given :py:class:`SBTarget`.

Breakpoint names provide a way to act on groups of breakpoints.  When you add a
name to a group of breakpoints, you can then use the name in all the command
line lldb commands for that name.  You can also configure the SBBreakpointName
options and those options will be propagated to any :py:class:`SBBreakpoint` s currently
using that name.  Adding a name to a breakpoint will also apply any of the
set options to that breakpoint.

You can also set permissions on a breakpoint name to disable listing, deleting
and disabling breakpoints.  That will disallow the given operation for breakpoints
except when the breakpoint is mentioned by ID.  So for instance deleting all the
breakpoints won't delete breakpoints so marked.

A name is created by constructing an SBBreakpointName for a target, or by adding
the name to a breakpoint with `SBBreakpoint.AddNameWithErrorHandling`::

    # Every breakpoint with this name will stop only on thread 1 and print a
    # backtrace.
    name = lldb.SBBreakpointName(target, 'my-breakpoints')
    name.SetThreadIndex(1)
    commands = lldb.SBStringList()
    commands.AppendString('bt')
    name.SetCommandLineCommands(commands)

    breakpoint = target.BreakpointCreateByName('main')
    breakpoint.AddNameWithErrorHandling('my-breakpoints')

See also :py:class:`SBBreakpoint`, `SBTarget.GetBreakpointNames` and
`SBTarget.FindBreakpointsByName`."
) lldb::SBBreakpointName;

%feature("docstring",
"Returns whether this object refers to a breakpoint name."
) lldb::SBBreakpointName::IsValid;

%feature("docstring",
"Returns the name as a string."
) lldb::SBBreakpointName::GetName;

%feature("docstring",
"Sets whether breakpoints with this name are enabled, see
`SBBreakpoint.SetEnabled`."
) lldb::SBBreakpointName::SetEnabled;

%feature("docstring",
"Returns whether breakpoints with this name are enabled."
) lldb::SBBreakpointName::IsEnabled;

%feature("docstring",
"Sets whether breakpoints with this name are deleted after being hit once, see
`SBBreakpoint.SetOneShot`."
) lldb::SBBreakpointName::SetOneShot;

%feature("docstring",
"Returns whether breakpoints with this name are one-shot breakpoints."
) lldb::SBBreakpointName::IsOneShot;

%feature("docstring",
"Sets the ignore count of breakpoints with this name, see
`SBBreakpoint.SetIgnoreCount`."
) lldb::SBBreakpointName::SetIgnoreCount;

%feature("docstring",
"Returns the ignore count of breakpoints with this name."
) lldb::SBBreakpointName::GetIgnoreCount;

%feature("docstring",
"Sets the condition of breakpoints with this name, see
`SBBreakpoint.SetCondition`."
) lldb::SBBreakpointName::SetCondition;

%feature("docstring",
"Returns the condition of breakpoints with this name."
) lldb::SBBreakpointName::GetCondition;

%feature("docstring",
"Sets whether the process continues automatically after breakpoints with this
name were hit, see `SBBreakpoint.SetAutoContinue`."
) lldb::SBBreakpointName::SetAutoContinue;

%feature("docstring",
"Returns whether the process continues automatically after a hit."
) lldb::SBBreakpointName::GetAutoContinue;

%feature("docstring",
"Restricts breakpoints with this name to the thread with the given thread ID,
see `SBBreakpoint.SetThreadID`."
) lldb::SBBreakpointName::SetThreadID;

%feature("docstring",
"Returns the thread ID breakpoints with this name are restricted to."
) lldb::SBBreakpointName::GetThreadID;

%feature("docstring",
"Restricts breakpoints with this name to the thread with the given index ID,
see `SBThread.GetIndexID`."
) lldb::SBBreakpointName::SetThreadIndex;

%feature("docstring",
"Returns the thread index breakpoints with this name are restricted to."
) lldb::SBBreakpointName::GetThreadIndex;

%feature("docstring",
"Restricts breakpoints with this name to threads with the given name."
) lldb::SBBreakpointName::SetThreadName;

%feature("docstring",
"Returns the thread name breakpoints with this name are restricted to."
) lldb::SBBreakpointName::GetThreadName;

%feature("docstring",
"Restricts breakpoints with this name to threads running on the given
libdispatch queue."
) lldb::SBBreakpointName::SetQueueName;

%feature("docstring",
"Returns the queue name breakpoints with this name are restricted to."
) lldb::SBBreakpointName::GetQueueName;

%feature("docstring",
"Sets the name of the script function that is called when a breakpoint with
this name is hit.

See `SBBreakpoint.SetScriptCallbackFunction` for the signature the function has
to have."
) lldb::SBBreakpointName::SetScriptCallbackFunction;

%feature("docstring",
"Sets the LLDB command line commands that are run when a breakpoint with this
name is hit.

See `SBBreakpoint.SetCommandLineCommands`."
) lldb::SBBreakpointName::SetCommandLineCommands;

%feature("docstring",
"Fills the given `SBStringList` with the commands of this breakpoint name.

Returns whether there are any commands."
) lldb::SBBreakpointName::GetCommandLineCommands;

%feature("docstring",
"Provides the body of the script function that is called when a breakpoint with
this name is hit.

See `SBBreakpoint.SetScriptCallbackBody`."
) lldb::SBBreakpointName::SetScriptCallbackBody;

%feature("docstring",
"Returns the help string of this breakpoint name.

The help string is shown in the ``breakpoint list`` output of breakpoints with
this name."
) lldb::SBBreakpointName::GetHelpString;

%feature("docstring",
"Sets the help string of this breakpoint name, see
`SBBreakpointName.GetHelpString`."
) lldb::SBBreakpointName::SetHelpString;

%feature("docstring",
"Returns whether breakpoints with this name are listed by the ``breakpoint
list`` command.

See `SBBreakpointName.SetAllowList`."
) lldb::SBBreakpointName::GetAllowList;

%feature("docstring",
"Sets whether breakpoints with this name may be listed.

If this is ``False``, breakpoints with this name are hidden from the
``breakpoint list`` command unless they are named explicitly by their ID. This
is useful to keep breakpoints that a script manages out of a user's way."
) lldb::SBBreakpointName::SetAllowList;

%feature("docstring",
"Returns whether breakpoints with this name may be deleted, see
`SBBreakpointName.SetAllowDelete`."
) lldb::SBBreakpointName::GetAllowDelete;

%feature("docstring",
"Sets whether breakpoints with this name may be deleted.

If this is ``False``, commands such as ``breakpoint delete`` skip breakpoints
with this name unless they are named explicitly by their ID."
) lldb::SBBreakpointName::SetAllowDelete;

%feature("docstring",
"Returns whether breakpoints with this name may be disabled, see
`SBBreakpointName.SetAllowDisable`."
) lldb::SBBreakpointName::GetAllowDisable;

%feature("docstring",
"Sets whether breakpoints with this name may be disabled.

If this is ``False``, commands such as ``breakpoint disable`` skip breakpoints
with this name unless they are named explicitly by their ID."
) lldb::SBBreakpointName::SetAllowDisable;

%feature("docstring",
"Writes a description of this breakpoint name into the given `SBStream`."
) lldb::SBBreakpointName::GetDescription;
