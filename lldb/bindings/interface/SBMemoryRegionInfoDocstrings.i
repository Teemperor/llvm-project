%feature("docstring",
"Describes one region of the address space of a process.

A memory region is a contiguous range of addresses with uniform permissions, for
example the code of a shared library or a mapped file. Regions are obtained from
`SBProcess.GetMemoryRegionInfo` for a specific address or from
`SBProcess.GetMemoryRegions` for all of them::

    region = lldb.SBMemoryRegionInfo()
    error = process.GetMemoryRegionInfo(addr, region)
    if error.Success() and region.IsWritable():
        process.WriteMemory(addr, b'\\x00', lldb.SBError())

API clients can get information about memory regions in processes.

For Python users, ``len()`` is overriden to output the size of the memory region
in bytes, and ``str()`` is overriden with the results of the
`SBMemoryRegionInfo.GetDescription` function: a formatted string that describes a
memory range in the form ``[Hex start - Hex End)`` with the associated
permissions (RWX)."
) lldb::SBMemoryRegionInfo;

%feature("docstring", "
        Returns whether this memory region has a list of modified (dirty)
        pages available or not.  When calling GetNumDirtyPages(), you will
        have 0 returned for both \"dirty page list is not known\" and 
        \"empty dirty page list\" (that is, no modified pages in this
        memory region).  You must use this method to disambiguate."
) lldb::SBMemoryRegionInfo::HasDirtyMemoryPageList;

%feature("docstring", "
        Return the number of dirty (modified) memory pages in this
        memory region, if available.  You must use the 
        SBMemoryRegionInfo::HasDirtyMemoryPageList() method to
        determine if a dirty memory list is available; it will depend
        on the target system can provide this information."
) lldb::SBMemoryRegionInfo::GetNumDirtyPages;

%feature("docstring", "
        Return the address of a modified, or dirty, page of memory.
        If the provided index is out of range, or this memory region 
        does not have dirty page information, LLDB_INVALID_ADDRESS 
        is returned."
) lldb::SBMemoryRegionInfo::GetDirtyPageAddressAtIndex;

%feature("docstring", "
        Return the size of pages in this memory region.  0 will be returned
        if this information was unavailable."
) lldb::SBMemoryRegionInfo::GetPageSize;

%feature("docstring", "
        Takes an SBStream parameter to write output to,
        formatted [Hex start - Hex End) with associated permissions (RWX).
        If the function results false, no output will be written. 
        If results true, the output will be written to the stream.
        "
) lldb::SBMemoryRegionInfo::GetDescription;

%feature("docstring",
"Resets this object to an invalid, empty region."
) lldb::SBMemoryRegionInfo::Clear;

%feature("docstring",
"Returns the first address of this region."
) lldb::SBMemoryRegionInfo::GetRegionBase;

%feature("docstring",
"Returns the address after the last byte of this region."
) lldb::SBMemoryRegionInfo::GetRegionEnd;

%feature("docstring",
"Returns whether this region can be read from.

Reading memory from a region that is not readable fails, see
`SBProcess.ReadMemory`."
) lldb::SBMemoryRegionInfo::IsReadable;

%feature("docstring",
"Returns whether this region can be written to.

Note that LLDB can often write to read-only regions anyway, since it writes
memory through the debug interface of the operating system rather than through
the process itself."
) lldb::SBMemoryRegionInfo::IsWritable;

%feature("docstring",
"Returns whether code in this region can be executed."
) lldb::SBMemoryRegionInfo::IsExecutable;

%feature("docstring",
"Returns whether this region is mapped into the address space of the process.

`SBProcess.GetMemoryRegionInfo` also returns regions for unmapped address
ranges, which is how the holes between the mapped regions can be found."
) lldb::SBMemoryRegionInfo::IsMapped;

%feature("docstring",
"Returns the name of this region, if it has one.

This is typically the path of the file that is mapped into the region. Returns
``None`` for anonymous regions."
) lldb::SBMemoryRegionInfo::GetName;