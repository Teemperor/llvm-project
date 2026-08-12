%feature("docstring",
"Represents a list of :py:class:`SBMemoryRegionInfo`.

The list of all memory regions of a process is returned by
`SBProcess.GetMemoryRegions`. In Python it supports ``len()`` and iteration::

    for region in process.GetMemoryRegions():
        if region.IsExecutable():
            print('%x-%x %s' % (region.GetRegionBase(), region.GetRegionEnd(),
                                region.GetName()))
"
) lldb::SBMemoryRegionInfoList;

%feature("docstring",
"Returns the number of memory regions in this list.

In Python this is also what ``len()`` returns."
) lldb::SBMemoryRegionInfoList::GetSize;

%feature("docstring",
"Finds the region that contains the given address.

Fills in the given `SBMemoryRegionInfo` and returns whether a region was found.
See `SBProcess.GetMemoryRegionInfo`, which queries the process directly."
) lldb::SBMemoryRegionInfoList::GetMemoryRegionContainingAddress;

%feature("docstring",
"Returns the region at the given index.

Fills in the given `SBMemoryRegionInfo` and returns whether the index was
valid."
) lldb::SBMemoryRegionInfoList::GetMemoryRegionAtIndex;

%feature("docstring",
"Appends a single `SBMemoryRegionInfo` or all regions of another
`SBMemoryRegionInfoList` to this list."
) lldb::SBMemoryRegionInfoList::Append;

%feature("docstring",
"Removes all regions from this list."
) lldb::SBMemoryRegionInfoList::Clear;
