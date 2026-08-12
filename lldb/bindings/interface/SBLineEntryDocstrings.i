%feature("docstring",
"Specifies an association with a contiguous range of instructions and
a source file location.

A line entry is one row of the line table of a compile unit: it says that the
instructions from `SBLineEntry.GetStartAddress` up to
`SBLineEntry.GetEndAddress` belong to a specific line and column of a source
file. This is what LLDB uses to show source code while stepping.

Line entries are obtained from `SBFrame.GetLineEntry`,
`SBAddress.GetLineEntry`, `SBSymbolContext.GetLineEntry` or from a compile
unit's line table (`SBCompileUnit.GetLineEntryAtIndex`).

:py:class:`SBCompileUnit` contains SBLineEntry(s). For example, ::

    for lineEntry in compileUnit:
        print('line entry: %s:%d' % (str(lineEntry.GetFileSpec()),
                                    lineEntry.GetLine()))
        print('start addr: %s' % str(lineEntry.GetStartAddress()))
        print('end   addr: %s' % str(lineEntry.GetEndAddress()))

produces: ::

    line entry: /Volumes/data/lldb/svn/trunk/test/python_api/symbol-context/main.c:20
    start addr: a.out[0x100000d98]
    end   addr: a.out[0x100000da3]
    line entry: /Volumes/data/lldb/svn/trunk/test/python_api/symbol-context/main.c:21
    start addr: a.out[0x100000da3]
    end   addr: a.out[0x100000da9]
    line entry: /Volumes/data/lldb/svn/trunk/test/python_api/symbol-context/main.c:22
    start addr: a.out[0x100000da9]
    end   addr: a.out[0x100000db6]
    line entry: /Volumes/data/lldb/svn/trunk/test/python_api/symbol-context/main.c:23
    start addr: a.out[0x100000db6]
    end   addr: a.out[0x100000dbc]
    ...

Note that the same source line can have several line entries, since a compiler
may produce code for one line in several places. Line entries can also be
created and filled in by hand (`SBLineEntry.SetFileSpec`,
`SBLineEntry.SetLine`), which is used by
`SBCompileUnit.FindLineEntryIndex`.

See also :py:class:`SBCompileUnit` and :py:class:`SBFileSpec`."
) lldb::SBLineEntry;

%feature("docstring",
"Returns whether this object refers to a line entry."
) lldb::SBLineEntry::IsValid;

%feature("docstring",
"Returns the first address of this line entry as an `SBAddress`."
) lldb::SBLineEntry::GetStartAddress;

%feature("docstring",
"Returns the address after the last byte of this line entry as an `SBAddress`."
) lldb::SBLineEntry::GetEndAddress;

%feature("docstring",
"Returns the end of the contiguous address range that belongs to the same
source line.

Line tables can contain several consecutive entries for one source line; this
walks them and returns the address after the last one. If
``include_inlined_functions`` is ``True``, entries of functions that were inlined
into this line are included as well. This is what ``thread step-over`` uses to
decide how far it has to run."
) lldb::SBLineEntry::GetSameLineContiguousAddressRangeEnd;

%feature("docstring",
"Returns the source file of this line entry as an `SBFileSpec`."
) lldb::SBLineEntry::GetFileSpec;

%feature("docstring",
"Returns the line number of this line entry.

Returns ``0`` if the line is unknown, which a compiler can emit for code that
does not belong to any source line."
) lldb::SBLineEntry::GetLine;

%feature("docstring",
"Returns the column number of this line entry.

Returns ``0`` if the debug information contains no column for it."
) lldb::SBLineEntry::GetColumn;

%feature("docstring",
"Sets the source file of this line entry.

Setting the file, line and column of a line entry is useful to build up a line
entry that is then passed to a lookup function such as
`SBCompileUnit.FindLineEntryIndex`; it does not change the debug information."
) lldb::SBLineEntry::SetFileSpec;

%feature("docstring",
"Sets the line number of this line entry, see
`SBLineEntry.SetFileSpec`."
) lldb::SBLineEntry::SetLine;

%feature("docstring",
"Sets the column number of this line entry, see
`SBLineEntry.SetFileSpec`."
) lldb::SBLineEntry::SetColumn;

%feature("docstring",
"Writes a description of this line entry into the given `SBStream`."
) lldb::SBLineEntry::GetDescription;
