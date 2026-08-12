%feature("docstring",
"Represents the environment of a certain process.

An environment is a set of ``name=value`` pairs. It is used to inspect the
environment a process runs in (`SBProcess.GetProcessInfo` and
`SBTarget.GetEnvironment`), the environment of a platform
(`SBPlatform.GetEnvironment`) and to set up the environment of a launch
(`SBLaunchInfo.SetEnvironment`).

Example: ::

  for entry in lldb.debugger.GetSelectedTarget().GetEnvironment().GetEntries():
    print(entry)

Modifying an environment that was returned by one of the getters does not
change anything, since those return copies. To change the environment of a
launch, pass the modified environment to `SBLaunchInfo.SetEnvironment`::

    env = target.GetEnvironment()
    env.Set('LD_PRELOAD', '/tmp/libfoo.so', True)
    launch_info = target.GetLaunchInfo()
    launch_info.SetEnvironment(env, False)
    target.SetLaunchInfo(launch_info)
") lldb::SBEnvironment;

%feature("docstring",
"Returns the value of the variable with the given name.

Returns ``None`` if there is no such variable."
) lldb::SBEnvironment::Get;

%feature("docstring",
"Returns the number of variables in this environment."
) lldb::SBEnvironment::GetNumValues;

%feature("docstring",
"Returns the name of the variable at the given index.

Returns ``None`` if the index is out of bounds."
) lldb::SBEnvironment::GetNameAtIndex;

%feature("docstring",
"Returns the value of the variable at the given index.

Returns ``None`` if the index is out of bounds."
) lldb::SBEnvironment::GetValueAtIndex;

%feature("docstring",
"Returns all variables of this environment as an `SBStringList`.

Each entry is a string of the form ``name=value``, which is the format
`SBLaunchInfo.SetEnvironmentEntries` expects."
) lldb::SBEnvironment::GetEntries;

%feature("docstring",
"Adds or replaces a variable given as a ``name=value`` string.

An existing variable with that name is overwritten. See
`SBEnvironment.Set` to pass the name and the value separately."
) lldb::SBEnvironment::PutEntry;

%feature("docstring",
"Replaces or extends this environment with the given entries.

``entries`` is an `SBStringList` of ``name=value`` strings. If ``append`` is
``False`` the existing variables are replaced, otherwise the given ones are
added and existing ones with the same name are overwritten."
) lldb::SBEnvironment::SetEntries;

%feature("docstring",
"Sets the variable with the given name to the given value.

If ``overwrite`` is ``False`` and a variable with that name already exists,
nothing happens. Returns whether the environment was changed::

    env.Set('DYLD_INSERT_LIBRARIES', '/tmp/libfoo.dylib', True)
"
) lldb::SBEnvironment::Set;

%feature("docstring",
"Removes the variable with the given name.

Returns whether a variable with that name existed."
) lldb::SBEnvironment::Unset;

%feature("docstring",
"Removes all variables from this environment."
) lldb::SBEnvironment::Clear;
