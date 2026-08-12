%feature("docstring",
"Describes how a target or program should be launched.

A launch info collects everything that influences a launch: the arguments and
the environment of the process, its working directory, where its standard input
and output go, and flags such as whether it should be stopped at the entry
point.

Pass one to `SBTarget.Launch` for full control over how a process is started, or
store it in the target with `SBTarget.SetLaunchInfo` so that later launches
(including the ones the ``run`` command performs) use it::

    launch_info = lldb.SBLaunchInfo(['--verbose', 'input.txt'])
    launch_info.SetWorkingDirectory('/tmp')
    launch_info.SetLaunchFlags(lldb.eLaunchFlagStopAtEntry)
    error = lldb.SBError()
    process = target.Launch(launch_info, error)

`SBTarget.LaunchSimple` is a shortcut for the common case that needs no special
options. See also :py:class:`SBAttachInfo` for attaching to an existing process
and :py:class:`SBEnvironment` for the environment of a launch."
) lldb::SBLaunchInfo;

%feature("docstring",
"Returns the process ID of the launched process.

Only meaningful after the launch. Returns
``lldb.LLDB_INVALID_PROCESS_ID`` otherwise."
) lldb::SBLaunchInfo::GetProcessID;

%feature("docstring",
"Returns the user ID the process is launched with, see
`SBLaunchInfo.SetUserID`."
) lldb::SBLaunchInfo::GetUserID;

%feature("docstring",
"Returns the group ID the process is launched with, see
`SBLaunchInfo.SetGroupID`."
) lldb::SBLaunchInfo::GetGroupID;

%feature("docstring",
"Returns whether a user ID was set, see `SBLaunchInfo.SetUserID`."
) lldb::SBLaunchInfo::UserIDIsValid;

%feature("docstring",
"Returns whether a group ID was set, see `SBLaunchInfo.SetGroupID`."
) lldb::SBLaunchInfo::GroupIDIsValid;

%feature("docstring",
"Sets the user ID the process should be launched with.

Only supported by platforms that can launch processes as a different user."
) lldb::SBLaunchInfo::SetUserID;

%feature("docstring",
"Sets the group ID the process should be launched with.

Only supported by platforms that can launch processes as a different group."
) lldb::SBLaunchInfo::SetGroupID;

%feature("docstring",
"Returns the executable that is launched as an `SBFileSpec`.

See `SBLaunchInfo.SetExecutableFile`."
) lldb::SBLaunchInfo::GetExecutableFile;

%feature("docstring",
"Set the executable file that will be used to launch the process and
optionally set it as the first argument in the argument vector.

This only needs to be specified if clients wish to carefully control
the exact path will be used to launch a binary. If you create a
target with a symlink, that symlink will get resolved in the target
and the resolved path will get used to launch the process. Calling
this function can help you still launch your process using the
path of your choice.

If this function is not called prior to launching with
`SBTarget.Launch`, the target will use the resolved executable
path that was used to create the target.

:param exe_file: The override path to use when launching the executable.
:param add_as_first_arg: If true, then the path will be inserted into the
    argument vector prior to launching. Otherwise the argument vector will be
    left alone."
) lldb::SBLaunchInfo::SetExecutableFile;

%feature("docstring",
"Get the listener that will be used to receive process events.

If no listener has been set via a call to
`SBLaunchInfo.SetListener`, then an invalid SBListener will be
returned (``SBListener.IsValid`` will return false). If a listener
has been set, then the valid listener object will be returned."
) lldb::SBLaunchInfo::GetListener;

%feature("docstring",
"Set the listener that will be used to receive process events.

By default the SBDebugger, which has a listener, that the SBTarget
belongs to will listen for the process events. Calling this function
allows a different listener to be used to listen for process events."
) lldb::SBLaunchInfo::SetListener;

%feature("docstring",
"Get the shadow listener that receives public process events,
additionally to the default process event listener.

If no listener has been set via a call to
`SBLaunchInfo.SetShadowListener`, then an invalid SBListener will
be returned (``SBListener.IsValid`` will return false). If a listener
has been set, then the valid listener object will be returned."
) lldb::SBLaunchInfo::GetShadowListener;

%feature("docstring",
"Set the shadow listener that will receive public process events,
additionally to the default process event listener.

By default a process has no shadow event listener.
Calling this function allows public process events to be broadcasted to an
additional listener on top of the default process event listener.
If the ``listener`` argument is invalid (``SBListener.IsValid`` will
return false), this will clear the shadow listener."
) lldb::SBLaunchInfo::SetShadowListener;

%feature("docstring",
"Returns the number of arguments the process is launched with.

This does not include the program name itself. See
`SBLaunchInfo.SetArguments`."
) lldb::SBLaunchInfo::GetNumArguments;

%feature("docstring",
"Returns the argument at the given index."
) lldb::SBLaunchInfo::GetArgumentAtIndex;

%feature("docstring",
"Sets the arguments the process is launched with.

In Python ``argv`` is a list of strings. If ``append`` is ``False`` the existing
arguments are replaced, otherwise the given ones are added::

    launch_info.SetArguments(['--verbose', 'input.txt'], False)
"
) lldb::SBLaunchInfo::SetArguments;

%feature("docstring",
"Returns the number of environment variables of this launch info.

See `SBLaunchInfo.GetEnvironment` for a more convenient way to inspect the
environment."
) lldb::SBLaunchInfo::GetNumEnvironmentEntries;

%feature("docstring",
"Returns the environment variable at the given index as a ``name=value``
string."
) lldb::SBLaunchInfo::GetEnvironmentEntryAtIndex;

%feature("docstring",
"Update this object with the given environment variables.

If append is false, the provided environment will replace the existing
environment. Otherwise, existing values will be updated or left untouched
accordingly.

:param envp: The new environment variables as a list of strings of the form
    ``name=value``.
:param append: Flag that controls whether to replace the existing environment.

For example::

    launch_info.SetEnvironmentEntries(['DYLD_INSERT_LIBRARIES=/tmp/lib.dylib'], True)
"
) lldb::SBLaunchInfo::SetEnvironmentEntries;

%feature("docstring",
"Update this object with the given environment variables.

If append is false, the provided environment will replace the existing
environment. Otherwise, existing values will be updated or left untouched
accordingly.

:param env: The new environment variables as an `SBEnvironment`.
:param append: Flag that controls whether to replace the existing environment."
) lldb::SBLaunchInfo::SetEnvironment;

%feature("docstring",
"Return the environment variables of this object.

:return: An `SBEnvironment` object which is a copy of the SBLaunchInfo's
    environment.
:rtype: SBEnvironment"
) lldb::SBLaunchInfo::GetEnvironment;

%feature("docstring",
"Resets this object to its default state."
) lldb::SBLaunchInfo::Clear;

%feature("docstring",
"Returns the directory the process is launched in."
) lldb::SBLaunchInfo::GetWorkingDirectory;

%feature("docstring",
"Sets the directory the process is launched in.

If this is not set, the working directory of the platform is used, see
`SBPlatform.GetWorkingDirectory`."
) lldb::SBLaunchInfo::SetWorkingDirectory;

%feature("docstring",
"Returns the launch flags as a bit mask of the ``lldb.eLaunchFlag*`` values.

See `SBLaunchInfo.SetLaunchFlags`."
) lldb::SBLaunchInfo::GetLaunchFlags;

%feature("docstring",
"Sets the launch flags.

``flags`` is a bit mask of the ``lldb.eLaunchFlag*`` values, for example
``lldb.eLaunchFlagStopAtEntry`` to stop the process at its entry point or
``lldb.eLaunchFlagDisableASLR`` to launch it at predictable addresses::

    launch_info.SetLaunchFlags(lldb.eLaunchFlagStopAtEntry |
                               lldb.eLaunchFlagDisableASLR)
"
) lldb::SBLaunchInfo::SetLaunchFlags;

%feature("docstring",
"Returns the name of the process plugin that is used for the launch."
) lldb::SBLaunchInfo::GetProcessPluginName;

%feature("docstring",
"Sets the process plugin that should be used for the launch.

Rarely needed; LLDB picks a fitting plugin on its own."
) lldb::SBLaunchInfo::SetProcessPluginName;

%feature("docstring",
"Returns the shell the process is launched through, if any."
) lldb::SBLaunchInfo::GetShell;

%feature("docstring",
"Sets a shell that is used to launch the process.

Launching through a shell makes it possible to use shell features such as
globbing and redirection in the arguments, see
`SBLaunchInfo.SetShellExpandArguments`."
) lldb::SBLaunchInfo::SetShell;

%feature("docstring",
"Returns whether the shell expands the arguments of the launch."
) lldb::SBLaunchInfo::GetShellExpandArguments;

%feature("docstring",
"Sets whether the shell should expand the arguments of the launch.

Requires a shell to be set with `SBLaunchInfo.SetShell`. With this enabled,
arguments containing ``*`` or ``~`` are expanded by the shell before the program
sees them."
) lldb::SBLaunchInfo::SetShellExpandArguments;

%feature("docstring",
"Returns how often the process is resumed after the launch."
) lldb::SBLaunchInfo::GetResumeCount;

%feature("docstring",
"Sets how often the process is resumed after the launch.

Used by launches that go through a shell, where the process stops several times
before it reaches the program that should be debugged."
) lldb::SBLaunchInfo::SetResumeCount;

%feature("docstring",
"Closes the given file descriptor in the launched process."
) lldb::SBLaunchInfo::AddCloseFileAction;

%feature("docstring",
"Makes a file descriptor of the launched process a duplicate of another.

This is the equivalent of ``dup2(dup_fd, fd)``, so
``AddDuplicateFileAction(2, 1)`` sends the standard error of the process to
wherever its standard output goes."
) lldb::SBLaunchInfo::AddDuplicateFileAction;

%feature("docstring",
"Opens a file as a file descriptor of the launched process.

Use this to redirect the standard input, output or error of the process::

    # Redirect stdout (fd 1) to a file.
    launch_info.AddOpenFileAction(1, '/tmp/stdout.txt', False, True)

``read`` and ``write`` say how the file is opened. Without any file actions the
process gets a pseudo terminal whose output can be read with
`SBProcess.GetSTDOUT`."
) lldb::SBLaunchInfo::AddOpenFileAction;

%feature("docstring",
"Suppresses a file descriptor of the launched process.

The file descriptor is connected to the null device, so output written to it is
discarded and reads return nothing."
) lldb::SBLaunchInfo::AddSuppressFileAction;

%feature("docstring",
"Sets platform specific data that is passed to the launch.

What the data means depends entirely on the platform that performs the launch."
) lldb::SBLaunchInfo::SetLaunchEventData;

%feature("docstring",
"Returns the platform specific launch data, see
`SBLaunchInfo.SetLaunchEventData`."
) lldb::SBLaunchInfo::GetLaunchEventData;

%feature("docstring",
"Returns whether LLDB detaches from the process if the launch runs into an
error."
) lldb::SBLaunchInfo::GetDetachOnError;

%feature("docstring",
"Sets whether LLDB detaches from the process if the launch runs into an error.

If this is ``False``, a process that could not be launched properly is killed
instead of being left running."
) lldb::SBLaunchInfo::SetDetachOnError;

%feature("docstring",
"Returns the name of the Python class that implements the scripted process."
) lldb::SBLaunchInfo::GetScriptedProcessClassName;

%feature("docstring",
"Sets the Python class that implements a scripted process.

Scripted processes are implemented in Python instead of by a process plugin, see
:doc:`/use/python-reference`. Together with
`SBLaunchInfo.SetScriptedProcessDictionary` this is how such a process is
launched."
) lldb::SBLaunchInfo::SetScriptedProcessClassName;

%feature("docstring",
"Returns the arguments that are passed to the scripted process as
`SBStructuredData`."
) lldb::SBLaunchInfo::GetScriptedProcessDictionary;

%feature("docstring",
"Sets the arguments that are passed to the scripted process.

``dict`` is an `SBStructuredData` dictionary that the scripted process
implementation receives, see
`SBLaunchInfo.SetScriptedProcessClassName`."
) lldb::SBLaunchInfo::SetScriptedProcessDictionary;
