%feature("docstring",
"Represents a contiguous range of addresses: a base address plus a byte size.

Address ranges are returned by API functions that describe how much memory
something occupies, for example `SBFunction.GetRanges` and `SBBlock.GetRanges`,
and they are what `SBProcess.FindRangesInMemory` searches in. A range can also
be constructed directly from an `SBAddress` and a size::

    range = lldb.SBAddressRange(function.GetStartAddress(), 0x100)
    print(range.GetBaseAddress().GetLoadAddress(target), range.GetByteSize())

See also :py:class:`SBAddress` and :py:class:`SBAddressRangeList`."
) lldb::SBAddressRange;

%feature("docstring",
"Resets this object to an invalid, empty range."
) lldb::SBAddressRange::Clear;

%feature("docstring",
"Returns whether this object describes an address range."
) lldb::SBAddressRange::IsValid;

%feature("docstring",
"Returns the first address of this range as an `SBAddress`."
) lldb::SBAddressRange::GetBaseAddress;

%feature("docstring",
"Returns the size of this range in bytes.

The range covers the addresses from the base address up to, but not including,
the base address plus this size."
) lldb::SBAddressRange::GetByteSize;

%feature("docstring",
"Writes a description of this range into the given `SBStream`.

``target`` is used to resolve the addresses of the range to load addresses and
symbols."
) lldb::SBAddressRange::GetDescription;
