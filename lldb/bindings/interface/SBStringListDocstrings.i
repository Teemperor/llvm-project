%feature("docstring",
"Represents a list of strings.

String lists are used by the API functions that hand out or take several strings,
for example `SBBreakpoint.SetCommandLineCommands`,
`SBTarget.GetBreakpointNames`, `SBEnvironment.GetEntries` and
`SBCommandInterpreter.HandleCompletion`.

In Python the list supports ``len()`` and iteration, and it can be built from
Python strings::

    commands = lldb.SBStringList()
    commands.AppendString('bt')
    commands.AppendString('frame variable')
    breakpoint.SetCommandLineCommands(commands)

    for name in target.GetBreakpointNames(lldb.SBStringList()):
        print(name)
"
) lldb::SBStringList;

%feature("docstring",
"Returns whether this object holds a list of strings."
) lldb::SBStringList::IsValid;

%feature("docstring",
"Appends a string to this list."
) lldb::SBStringList::AppendString;

%feature("docstring",
"Appends all strings of another `SBStringList` to this list.

The overload that takes a list of strings appends those instead."
) lldb::SBStringList::AppendList;

%feature("docstring",
"Returns the number of strings in this list.

In Python this is also what ``len()`` returns."
) lldb::SBStringList::GetSize;

%feature("docstring",
"Returns the string at the given index.

Returns ``None`` if the index is out of bounds."
) lldb::SBStringList::GetStringAtIndex;

%feature("docstring",
"Removes all strings from this list."
) lldb::SBStringList::Clear;
