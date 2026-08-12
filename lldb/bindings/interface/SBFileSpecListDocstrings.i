%feature("docstring",
"Represents a list of :py:class:`SBFileSpec`.

File spec lists are used by the API functions that filter by file, most notably
the breakpoint creation functions of `SBTarget`, which take a list of modules and
a list of compile units to search::

    modules = lldb.SBFileSpecList()
    modules.Append(lldb.SBFileSpec('a.out'))
    files = lldb.SBFileSpecList()
    breakpoint = target.BreakpointCreateByRegex('^test_', lldb.eLanguageTypeUnknown,
                                                modules, files)

An empty list means \"don\'t filter\". In Python the list supports ``len()``,
indexing and iteration."
) lldb::SBFileSpecList;

%feature("docstring",
"Returns the number of file specifications in this list.

In Python this is also what ``len()`` returns."
) lldb::SBFileSpecList::GetSize;

%feature("docstring",
"Writes a description of all file specifications in this list into the given
`SBStream`."
) lldb::SBFileSpecList::GetDescription;

%feature("docstring",
"Appends an `SBFileSpec` to this list."
) lldb::SBFileSpecList::Append;

%feature("docstring",
"Appends an `SBFileSpec` to this list if it isn\'t in it yet.

Returns whether the file specification was appended."
) lldb::SBFileSpecList::AppendIfUnique;

%feature("docstring",
"Removes all file specifications from this list."
) lldb::SBFileSpecList::Clear;

%feature("docstring",
"Returns the index of the given file specification in this list.

The search starts at ``idx``. If ``full`` is ``True`` the whole path has to match,
otherwise matching the file name is enough. Returns ``lldb.UINT32_MAX`` if the
file was not found."
) lldb::SBFileSpecList::FindFileIndex;

%feature("docstring",
"Returns the file specification at the given index as an `SBFileSpec`."
) lldb::SBFileSpecList::GetFileSpecAtIndex;
