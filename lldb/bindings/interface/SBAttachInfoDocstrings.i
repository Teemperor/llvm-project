%feature("docstring",
"Describes how to attach when calling :py:class:`SBTarget.Attach`.

An attach info identifies the process to attach to, either by process ID
(`SBAttachInfo.SetProcessID`) or by executable name together with
`SBAttachInfo.SetWaitForLaunch`, and it carries the options of the attach::

    # Attach to a running process by ID.
    attach_info = lldb.SBAttachInfo(pid)
    error = lldb.SBError()
    process = target.Attach(attach_info, error)

    # Wait for the next process called 'a.out' to be launched and attach to it.
    attach_info = lldb.SBAttachInfo()
    attach_info.SetExecutable('a.out')
    attach_info.SetWaitForLaunch(True)
    process = target.Attach(attach_info, error)

`SBTarget.AttachToProcessWithID` and
`SBTarget.AttachToProcessWithName` are shortcuts for the common cases. See
also :py:class:`SBLaunchInfo` for starting a new process."
) lldb::SBAttachInfo;

%feature("docstring",
"Returns the process ID to attach to.

Returns ``lldb.LLDB_INVALID_PROCESS_ID`` if the process is identified by name
instead."
) lldb::SBAttachInfo::GetProcessID;

%feature("docstring",
"Sets the process ID to attach to."
) lldb::SBAttachInfo::SetProcessID;

%feature("docstring",
"Sets the name or path of the executable to attach to.

Takes either a path as a string or an `SBFileSpec`. Only the file name is
matched against the running processes, so a full path is not required. Use this
together with `SBAttachInfo.SetWaitForLaunch` to wait for a process to show
up."
) lldb::SBAttachInfo::SetExecutable;

%feature("docstring",
"Returns whether attaching waits for the process to be launched.

See `SBAttachInfo.SetWaitForLaunch`."
) lldb::SBAttachInfo::GetWaitForLaunch;

%feature("docstring",
"Set attach by process name settings.

Designed to be used after a call to `SBAttachInfo.SetExecutable`.

:param b: If ``False``, attach to an existing process whose name matches.
    If ``True``, then wait for the next process whose name matches.
:param async: If ``False``, then the `SBTarget.Attach` call will be a
    synchronous call with no way to cancel the attach in progress.
    If ``True``, then the `SBTarget.Attach` function will
    return immediately and clients are expected to wait for a
    process ``lldb.eStateStopped`` event if a suitable process is
    eventually found. If the client wants to cancel the attach,
    `SBProcess.Stop` can be called and an ``lldb.eStateExited`` process
    event will be delivered.

The overload that does not take ``async`` implies a synchronous attach."
) lldb::SBAttachInfo::SetWaitForLaunch;

%feature("docstring",
"Returns whether already running processes are ignored when waiting for a
launch.

See `SBAttachInfo.SetIgnoreExisting`."
) lldb::SBAttachInfo::GetIgnoreExisting;

%feature("docstring",
"Sets whether already running processes are ignored when waiting for a launch.

Only meaningful together with `SBAttachInfo.SetWaitForLaunch`: with this
enabled, a process that is already running when the attach starts does not count
as a match."
) lldb::SBAttachInfo::SetIgnoreExisting;

%feature("docstring",
"Returns how often the process is resumed after the attach."
) lldb::SBAttachInfo::GetResumeCount;

%feature("docstring",
"Sets how often the process is resumed after the attach."
) lldb::SBAttachInfo::SetResumeCount;

%feature("docstring",
"Returns the name of the process plugin that is used for the attach."
) lldb::SBAttachInfo::GetProcessPluginName;

%feature("docstring",
"Sets the process plugin that should be used for the attach.

Rarely needed; LLDB picks a fitting plugin on its own."
) lldb::SBAttachInfo::SetProcessPluginName;

%feature("docstring",
"Returns the user ID of the process to attach to, see
`SBAttachInfo.SetUserID`."
) lldb::SBAttachInfo::GetUserID;

%feature("docstring",
"Returns the group ID of the process to attach to, see
`SBAttachInfo.SetGroupID`."
) lldb::SBAttachInfo::GetGroupID;

%feature("docstring",
"Returns whether a user ID was set, see `SBAttachInfo.SetUserID`."
) lldb::SBAttachInfo::UserIDIsValid;

%feature("docstring",
"Returns whether a group ID was set, see `SBAttachInfo.SetGroupID`."
) lldb::SBAttachInfo::GroupIDIsValid;

%feature("docstring",
"Sets the user ID of the process to attach to.

Used to disambiguate when several users run a process with the same name."
) lldb::SBAttachInfo::SetUserID;

%feature("docstring",
"Sets the group ID of the process to attach to."
) lldb::SBAttachInfo::SetGroupID;

%feature("docstring",
"Returns the effective user ID of the process to attach to."
) lldb::SBAttachInfo::GetEffectiveUserID;

%feature("docstring",
"Returns the effective group ID of the process to attach to."
) lldb::SBAttachInfo::GetEffectiveGroupID;

%feature("docstring",
"Returns whether an effective user ID was set."
) lldb::SBAttachInfo::EffectiveUserIDIsValid;

%feature("docstring",
"Returns whether an effective group ID was set."
) lldb::SBAttachInfo::EffectiveGroupIDIsValid;

%feature("docstring",
"Sets the effective user ID of the process to attach to."
) lldb::SBAttachInfo::SetEffectiveUserID;

%feature("docstring",
"Sets the effective group ID of the process to attach to."
) lldb::SBAttachInfo::SetEffectiveGroupID;

%feature("docstring",
"Returns the process ID of the parent of the process to attach to."
) lldb::SBAttachInfo::GetParentProcessID;

%feature("docstring",
"Sets the process ID of the parent of the process to attach to.

Used to disambiguate when several processes with the same name are running and
only the child of a specific process is of interest."
) lldb::SBAttachInfo::SetParentProcessID;

%feature("docstring",
"Returns whether a parent process ID was set, see
`SBAttachInfo.SetParentProcessID`."
) lldb::SBAttachInfo::ParentProcessIDIsValid;

%feature("docstring",
"Returns the listener that will receive the process events.

Returns an invalid `SBListener` if none was set, in which case the debugger's
listener is used. See `SBAttachInfo.SetListener`."
) lldb::SBAttachInfo::GetListener;

%feature("docstring",
"Sets the listener that will receive the process events.

By default the listener of the `SBDebugger` the target belongs to receives the
events of the attached process."
) lldb::SBAttachInfo::SetListener;

%feature("docstring",
"Returns the shadow listener that additionally receives public process events.

See `SBAttachInfo.SetShadowListener`."
) lldb::SBAttachInfo::GetShadowListener;

%feature("docstring",
"Sets a shadow listener that additionally receives public process events.

The shadow listener receives the public events of the process on top of the
default event listener. Passing an invalid `SBListener` clears it."
) lldb::SBAttachInfo::SetShadowListener;

%feature("docstring",
"Returns the name of the Python class that implements the scripted process."
) lldb::SBAttachInfo::GetScriptedProcessClassName;

%feature("docstring",
"Sets the Python class that implements a scripted process.

See `SBLaunchInfo.SetScriptedProcessClassName`."
) lldb::SBAttachInfo::SetScriptedProcessClassName;

%feature("docstring",
"Returns the arguments that are passed to the scripted process as
`SBStructuredData`."
) lldb::SBAttachInfo::GetScriptedProcessDictionary;

%feature("docstring",
"Sets the arguments that are passed to the scripted process.

See `SBLaunchInfo.SetScriptedProcessDictionary`."
) lldb::SBAttachInfo::SetScriptedProcessDictionary;
