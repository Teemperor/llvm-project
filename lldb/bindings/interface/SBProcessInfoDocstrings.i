%feature("docstring",
"Describes an existing process and any discoverable information that pertains to
that process.

Process infos are returned by `SBProcess.GetProcessInfo` for the process that is
being debugged and by `SBPlatform.GetAllProcesses` for the processes that run on
a platform, which is how a process to attach to can be found::

    error = lldb.SBError()
    for info in platform.GetAllProcesses(error):
        if info.GetName() == 'a.out':
            attach_info = lldb.SBAttachInfo(info.GetProcessID())

See also :py:class:`SBProcessInfoList` and :py:class:`SBAttachInfo`."
) lldb::SBProcessInfo;

%feature("docstring",
"Returns whether this object describes a process."
) lldb::SBProcessInfo::IsValid;

%feature("docstring",
"Returns the name of the process, i.e. the file name of its executable."
) lldb::SBProcessInfo::GetName;

%feature("docstring",
"Returns the executable of the process as an `SBFileSpec`."
) lldb::SBProcessInfo::GetExecutableFile;

%feature("docstring",
"Returns the first argument of the process, i.e. the program name as the process
itself sees it.

This can differ from `SBProcessInfo.GetName`, for example for processes that
change their own argument vector."
) lldb::SBProcessInfo::GetArg0;

%feature("docstring",
"Returns the process ID of the process."
) lldb::SBProcessInfo::GetProcessID;

%feature("docstring",
"Returns the user ID the process runs as."
) lldb::SBProcessInfo::GetUserID;

%feature("docstring",
"Returns the group ID the process runs as."
) lldb::SBProcessInfo::GetGroupID;

%feature("docstring",
"Returns whether the user ID of the process is known."
) lldb::SBProcessInfo::UserIDIsValid;

%feature("docstring",
"Returns whether the group ID of the process is known."
) lldb::SBProcessInfo::GroupIDIsValid;

%feature("docstring",
"Returns the effective user ID of the process."
) lldb::SBProcessInfo::GetEffectiveUserID;

%feature("docstring",
"Returns the effective group ID of the process."
) lldb::SBProcessInfo::GetEffectiveGroupID;

%feature("docstring",
"Returns whether the effective user ID of the process is known."
) lldb::SBProcessInfo::EffectiveUserIDIsValid;

%feature("docstring",
"Returns whether the effective group ID of the process is known."
) lldb::SBProcessInfo::EffectiveGroupIDIsValid;

%feature("docstring",
"Returns the process ID of the parent of the process."
) lldb::SBProcessInfo::GetParentProcessID;

%feature("docstring",
"Return the target triple (arch-vendor-os) for the described process."
) lldb::SBProcessInfo::GetTriple;

%feature("docstring",
"Returns the number of command line arguments of the process.

This does not include the program name, see `SBProcessInfo.GetArg0`."
) lldb::SBProcessInfo::GetNumArguments;

%feature("docstring",
"Returns the command line argument at the given index."
) lldb::SBProcessInfo::GetArgumentAtIndex;
