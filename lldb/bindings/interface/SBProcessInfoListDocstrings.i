%feature("docstring",
"Represents a list of :py:class:`SBProcessInfo`.

Process info lists are returned by `SBPlatform.GetAllProcesses`, which is how the
processes that could be attached to are found::

    error = lldb.SBError()
    for info in platform.GetAllProcesses(error):
        print('%d %s' % (info.GetProcessID(), info.GetName()))

In Python the list supports ``len()``, indexing and iteration."
) lldb::SBProcessInfoList;

%feature("docstring",
"Returns the number of process infos in this list.

In Python this is also what ``len()`` returns."
) lldb::SBProcessInfoList::GetSize;

%feature("docstring",
"Returns the process info at the given index.

Fills in the given `SBProcessInfo` and returns whether the index was valid."
) lldb::SBProcessInfoList::GetProcessInfoAtIndex;

%feature("docstring",
"Removes all process infos from this list."
) lldb::SBProcessInfoList::Clear;
