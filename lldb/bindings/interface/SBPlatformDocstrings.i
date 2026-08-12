%feature("docstring",
"Describes how :py:class:`SBPlatform.ConnectRemote` connects to a remote platform.

At a minimum the URL of the remote platform has to be set::

    options = lldb.SBPlatformConnectOptions('connect://remote.host:1234')
    error = platform.ConnectRemote(options)
"
) lldb::SBPlatformConnectOptions;

%feature("docstring",
"Returns the URL this connection uses, e.g. ``connect://localhost:1234``."
) lldb::SBPlatformConnectOptions::GetURL;

%feature("docstring",
"Sets the URL to connect to, see `SBPlatformConnectOptions.GetURL`."
) lldb::SBPlatformConnectOptions::SetURL;

%feature("docstring",
"Returns whether files are transferred with ``rsync``.

See `SBPlatformConnectOptions.EnableRsync`."
) lldb::SBPlatformConnectOptions::GetRsyncEnabled;

%feature("docstring",
"Makes the connection use ``rsync`` to transfer files.

``options`` are extra command line options for ``rsync``,
``remote_path_prefix`` is prepended to remote paths and
``omit_hostname_from_remote_path`` controls whether the host name is left out of
the ``rsync`` paths."
) lldb::SBPlatformConnectOptions::EnableRsync;

%feature("docstring",
"Makes the connection transfer files without ``rsync``, see
`SBPlatformConnectOptions.EnableRsync`."
) lldb::SBPlatformConnectOptions::DisableRsync;

%feature("docstring",
"Returns the directory in which files from the remote system are cached."
) lldb::SBPlatformConnectOptions::GetLocalCacheDirectory;

%feature("docstring",
"Sets the directory in which files from the remote system are cached.

Modules that are downloaded from the remote platform are kept there so they
don't have to be transferred again."
) lldb::SBPlatformConnectOptions::SetLocalCacheDirectory;

%feature("docstring",
"Represents a shell command that can be run by :py:class:`SBPlatform.Run`.

The command carries its own working directory and timeout, and after running it
holds the exit status and the output::

    command = lldb.SBPlatformShellCommand('ls /tmp')
    platform.Run(command)
    print(command.GetStatus(), command.GetOutput())
"
) lldb::SBPlatformShellCommand;

%feature("docstring",
"Resets this object, clearing the command as well as any results."
) lldb::SBPlatformShellCommand::Clear;

%feature("docstring",
"Returns the shell that is used to run the command, e.g. ``/bin/sh``."
) lldb::SBPlatformShellCommand::GetShell;

%feature("docstring",
"Sets the shell that is used to run the command.

If no shell is set, the command is run directly."
) lldb::SBPlatformShellCommand::SetShell;

%feature("docstring",
"Returns the command that is run."
) lldb::SBPlatformShellCommand::GetCommand;

%feature("docstring",
"Sets the command that is run."
) lldb::SBPlatformShellCommand::SetCommand;

%feature("docstring",
"Returns the directory the command is run in."
) lldb::SBPlatformShellCommand::GetWorkingDirectory;

%feature("docstring",
"Sets the directory the command is run in."
) lldb::SBPlatformShellCommand::SetWorkingDirectory;

%feature("docstring",
"Returns after how many seconds running the command is given up on."
) lldb::SBPlatformShellCommand::GetTimeoutSeconds;

%feature("docstring",
"Sets after how many seconds running the command is given up on."
) lldb::SBPlatformShellCommand::SetTimeoutSeconds;

%feature("docstring",
"Returns the signal that killed the command, if it was killed by one.

Only meaningful after `SBPlatform.Run` was called with this command."
) lldb::SBPlatformShellCommand::GetSignal;

%feature("docstring",
"Returns the exit status of the command.

Only meaningful after `SBPlatform.Run` was called with this command."
) lldb::SBPlatformShellCommand::GetStatus;

%feature("docstring",
"Returns the output the command produced.

Only meaningful after `SBPlatform.Run` was called with this command."
) lldb::SBPlatformShellCommand::GetOutput;

%feature("docstring",
"A class that represents a platform that can represent the current host or a remote host debug platform.

The SBPlatform class represents the current host, or a remote host.
It can be connected to a remote platform in order to provide ways
to remotely launch and attach to processes, upload/download files,
create directories, run remote shell commands, find locally cached
versions of files from the remote system, and much more.

SBPlatform objects can be created and then used to connect to a remote
platform which allows the SBPlatform to be used to get a list of the
current processes on the remote host, attach to one of those processes,
install programs on the remote system, attach and launch processes,
and much more.

Every :py:class:`SBTarget` has a corresponding SBPlatform. The platform can be
specified upon target creation, or the currently selected platform
will attempt to be used when creating the target automatically as long
as the currently selected platform matches the target architecture
and executable type. If the architecture or executable type do not match,
a suitable platform will be found automatically.

For example, connecting to a remote system and listing its processes::

    platform = lldb.SBPlatform('remote-linux')
    debugger.SetSelectedPlatform(platform)
    error = platform.ConnectRemote(lldb.SBPlatformConnectOptions('connect://remote.host:1234'))
    if error.Success():
        for process_info in platform.GetAllProcesses(lldb.SBError()):
            print('%d %s' % (process_info.GetProcessID(), process_info.GetName()))

See also `SBTarget.GetPlatform`, `SBDebugger.GetSelectedPlatform` and
:py:class:`SBPlatformConnectOptions`."

) lldb::SBPlatform;

%feature("docstring",
"Returns the platform that represents the system LLDB itself runs on.

This is a class method::

    print(lldb.SBPlatform.GetHostPlatform().GetTriple())
"
) lldb::SBPlatform::GetHostPlatform;

%feature("docstring",
"Returns whether this object refers to a platform."
) lldb::SBPlatform::IsValid;

%feature("docstring",
"Returns true if this platform is the host platform, otherwise false."
) lldb::SBPlatform::IsHost;

%feature("docstring",
"Resets this object to an invalid platform."
) lldb::SBPlatform::Clear;

%feature("docstring",
"Returns the working directory of this platform.

This is the directory that processes are launched in and that relative paths of
the file transfer functions are resolved against."
) lldb::SBPlatform::GetWorkingDirectory;

%feature("docstring",
"Sets the working directory of this platform, see
`SBPlatform.GetWorkingDirectory`."
) lldb::SBPlatform::SetWorkingDirectory;

%feature("docstring",
"Returns the name of this platform, e.g. ``host`` or ``remote-linux``.

The names are the ones the ``platform list`` command prints."
) lldb::SBPlatform::GetName;

%feature("docstring",
"Connects this platform to a remote system and returns an `SBError`.

``connect_options`` is an `SBPlatformConnectOptions` that at least carries the
URL to connect to. See `SBPlatform.DisconnectRemote`."
) lldb::SBPlatform::ConnectRemote;

%feature("docstring",
"Disconnects this platform from the remote system it is connected to."
) lldb::SBPlatform::DisconnectRemote;

%feature("docstring",
"Returns whether this platform is connected to a remote system.

Always ``True`` for the host platform."
) lldb::SBPlatform::IsConnected;

%feature("docstring",
"Returns the triple of the system this platform represents, e.g.
``x86_64-unknown-linux-gnu``."
) lldb::SBPlatform::GetTriple;

%feature("docstring",
"Returns the host name of the system this platform represents."
) lldb::SBPlatform::GetHostname;

%feature("docstring",
"Returns the build string of the operating system, e.g. a Darwin build number."
) lldb::SBPlatform::GetOSBuild;

%feature("docstring",
"Returns a human readable description of the operating system."
) lldb::SBPlatform::GetOSDescription;

%feature("docstring",
"Returns the major version number of the operating system."
) lldb::SBPlatform::GetOSMajorVersion;

%feature("docstring",
"Returns the minor version number of the operating system."
) lldb::SBPlatform::GetOSMinorVersion;

%feature("docstring",
"Returns the update (patch) version number of the operating system."
) lldb::SBPlatform::GetOSUpdateVersion;

%feature("docstring",
"Sets the SDK root that is used to find modules and headers of this platform.

Used when debugging against a specific SDK, for example when the binaries of the
remote system are available in a locally mounted SDK directory."
) lldb::SBPlatform::SetSDKRoot;

%feature("docstring",
"Copies a file from the local system to this platform.

``src`` and ``dst`` are `SBFileSpec` objects; ``src`` is the local file and
``dst`` the destination on the platform. Returns an `SBError`::

    error = platform.Put(lldb.SBFileSpec('/tmp/local.txt'),
                         lldb.SBFileSpec('/tmp/remote.txt'))

See `SBPlatform.Get` for the other direction and `SBPlatform.Install` for
installing a program together with its dependencies."
) lldb::SBPlatform::Put;

%feature("docstring",
"Copies a file from this platform to the local system.

See `SBPlatform.Put`."
) lldb::SBPlatform::Get;

%feature("docstring",
"Installs an executable or shared library on this platform.

Unlike `SBPlatform.Put` this also makes the file executable and remembers where
it was installed, so the target can find it. See `SBTarget.Install`."
) lldb::SBPlatform::Install;

%feature("docstring",
"Runs a shell command on this platform.

``shell_command`` is an `SBPlatformShellCommand` that also receives the output
and the exit status of the command::

    command = lldb.SBPlatformShellCommand('uname -a')
    error = platform.Run(command)
    print(command.GetOutput())
"
) lldb::SBPlatform::Run;

%feature("docstring",
"Launches a process on this platform without debugging it.

``launch_info`` is an `SBLaunchInfo` describing what to launch. Use
`SBTarget.Launch` to launch a process under the debugger's control."
) lldb::SBPlatform::Launch;

%feature("docstring",
"Attaches to a process on this platform and returns it as an `SBProcess`.

``attach_info`` is an `SBAttachInfo`, ``debugger`` the `SBDebugger` that should
own the resulting process and ``error`` is filled in on failure. See
`SBTarget.Attach` for attaching within an existing target."
) lldb::SBPlatform::Attach;

%feature("docstring",
"Returns the processes running on this platform as an `SBProcessInfoList`.

``error`` is filled in if the list could not be obtained::

    error = lldb.SBError()
    for info in platform.GetAllProcesses(error):
        print('%d %s' % (info.GetProcessID(), info.GetName()))
"
) lldb::SBPlatform::GetAllProcesses;

%feature("docstring",
"Kills the process with the given process ID on this platform."
) lldb::SBPlatform::Kill;

%feature("docstring",
"Creates a directory on this platform.

``file_permissions`` is a bit mask of the ``lldb.eFilePermissions*`` values.
Returns an `SBError`."
) lldb::SBPlatform::MakeDirectory;

%feature("docstring",
"Returns the permissions of a file on this platform as a bit mask of the
``lldb.eFilePermissions*`` values."
) lldb::SBPlatform::GetFilePermissions;

%feature("docstring",
"Sets the permissions of a file on this platform.

``file_permissions`` is a bit mask of the ``lldb.eFilePermissions*`` values.
Returns an `SBError`."
) lldb::SBPlatform::SetFilePermissions;

%feature("docstring",
"Returns the `SBUnixSignals` of this platform.

Use it to look up the signal numbers and names of the platform's operating
system."
) lldb::SBPlatform::GetUnixSignals;

%feature("docstring",
"Returns the environment variables of the remote platform connection process.

The result is a copy of the platform's environment as an `SBEnvironment`."
) lldb::SBPlatform::GetEnvironment;

%feature("docstring",
"Installs a callback that locates modules and symbol files.

Use this to implement a custom module cache, for example to fetch binaries from
a build system or symbol files from a symbol server instead of downloading them
from the remote system. The target calls the callback to get a module file and a
symbol file, and falls back to LLDB's own implementation when the callback fails
or returns a file that does not exist. Passing ``None`` clears the callback.

In Python the callback takes three arguments: the `SBModuleSpec` of the module
that is being looked for, an `SBFileSpec` for the module file and an `SBFileSpec`
for the symbol file. It fills in one or both of the file specs and returns an
`SBError`::

    def locate_module(module_spec, module_file_spec, symbol_file_spec):
        path = my_cache.find(module_spec.GetUUIDString())
        if path is None:
            return lldb.SBError('not in cache')
        module_file_spec.SetDirectory(os.path.dirname(path))
        module_file_spec.SetFilename(os.path.basename(path))
        return lldb.SBError()

    platform.SetLocateModuleCallback(locate_module)
"
) lldb::SBPlatform::SetLocateModuleCallback;
