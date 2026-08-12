%feature("docstring",
"Represents a list of :py:class:`SBAddressRange`.

Range lists are returned by functions that can describe several ranges at once,
such as `SBFunction.GetRanges` and `SBBlock.GetRanges`, and they are what
`SBProcess.FindRangesInMemory` takes and returns.

In Python the list supports ``len()``, indexing and iteration::

    for range in function.GetRanges():
        print('%s + %d' % (range.GetBaseAddress(), range.GetByteSize()))
"
) lldb::SBAddressRangeList;

%feature("docstring",
"Returns the number of ranges in this list.

In Python this is also what ``len()`` returns."
) lldb::SBAddressRangeList::GetSize;

%feature("docstring",
"Removes all ranges from this list."
) lldb::SBAddressRangeList::Clear;

%feature("docstring",
"Returns the range at the given index as an `SBAddressRange`."
) lldb::SBAddressRangeList::GetAddressRangeAtIndex;

%feature("docstring",
"Appends a single `SBAddressRange` or all ranges of another
`SBAddressRangeList` to this list."
) lldb::SBAddressRangeList::Append;

%feature("docstring",
"Writes a description of all ranges in this list into the given `SBStream`.

``target`` is used to resolve the addresses to load addresses and symbols."
) lldb::SBAddressRangeList::GetDescription;
