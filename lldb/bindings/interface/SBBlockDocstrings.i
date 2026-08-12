%feature("docstring",
"Represents a lexical block of a function.

A block is a scope in the source code: the body of a function, the body of an
``if`` statement or the code that resulted from inlining a function call. Blocks
form a tree whose root is the function's top level block
(`SBFunction.GetBlock`), and each block knows the variables that are declared in
it (`SBBlock.GetVariables`).

`SBFunction` contains SBBlock(s), and `SBFrame.GetBlock` returns the innermost
block of a frame's program counter. Walking outwards from there shows the chain
of inlined calls::

    block = frame.GetBlock()
    while block:
        if block.IsInlined():
            print('inlined %s called from %s:%d' % (block.GetInlinedName(),
                                                    block.GetInlinedCallSiteFile(),
                                                    block.GetInlinedCallSiteLine()))
        block = block.GetParent()

See also :py:class:`SBFunction` and :py:class:`SBFrame`."
) lldb::SBBlock;

%feature("docstring",
"Returns whether this object refers to a lexical block."
) lldb::SBBlock::IsValid;

%feature("docstring",
"Is this block contained within an inlined function?"
) lldb::SBBlock::IsInlined;

%feature("docstring", "
    Get the function name if this block represents an inlined function;
    otherwise, return None.") lldb::SBBlock::GetInlinedName;

%feature("docstring", "
    Get the call site file if this block represents an inlined function;
    otherwise, return an invalid file spec.") lldb::SBBlock::GetInlinedCallSiteFile;

%feature("docstring", "
    Get the call site line if this block represents an inlined function;
    otherwise, return 0.") lldb::SBBlock::GetInlinedCallSiteLine;

%feature("docstring", "
    Get the call site column if this block represents an inlined function;
    otherwise, return 0.") lldb::SBBlock::GetInlinedCallSiteColumn;

%feature("docstring", "
    Get the parent block.

    Returns an invalid block for the top level block of a function.") lldb::SBBlock::GetParent;

%feature("docstring", "
    Get the inlined block that is or contains this block.

    Returns this block if it is an inlined function's block, otherwise the
    closest such block above it, or an invalid block if this block is not inside
    an inlined function. See `SBBlock.IsInlined`."
) lldb::SBBlock::GetContainingInlinedBlock;

%feature("docstring", "
    Get the next sibling block of this block.

    Together with `SBBlock.GetFirstChild` this can be used to walk all blocks of
    a function.") lldb::SBBlock::GetSibling;

%feature("docstring", "
    Get the first block that is nested inside this block.

    See `SBBlock.GetSibling` for iterating over all children.") lldb::SBBlock::GetFirstChild;

%feature("docstring",
"Returns the number of address ranges that belong to this block.

A block usually covers one contiguous range of addresses, but optimized code can
be split into several ranges."
) lldb::SBBlock::GetNumRanges;

%feature("docstring",
"Returns the first address of the range with the given index as an `SBAddress`.

See `SBBlock.GetNumRanges`."
) lldb::SBBlock::GetRangeStartAddress;

%feature("docstring",
"Returns the address after the last byte of the range with the given index.

See `SBBlock.GetNumRanges`."
) lldb::SBBlock::GetRangeEndAddress;

%feature("docstring",
"Returns all address ranges of this block as an `SBAddressRangeList`."
) lldb::SBBlock::GetRanges;

%feature("docstring",
"Returns the index of the range that contains the given address.

Returns ``lldb.UINT32_MAX`` if no range of this block contains that address.
The index can be passed to `SBBlock.GetRangeStartAddress` and
`SBBlock.GetRangeEndAddress`."
) lldb::SBBlock::GetRangeIndexForBlockAddress;

%feature("docstring",
"Returns the variables of this block as an `SBValueList`.

The boolean parameters select which kinds of variables are returned:
``arguments`` for the parameters of the function, ``locals`` for local variables
and ``statics`` for static variables. Pass an `SBFrame` to get the values of the
variables in that frame, or an `SBTarget` to get the variables without a frame,
which is useful for static and global variables::

    for var in block.GetVariables(frame, True, True, False, lldb.eNoDynamicValues):
        print('%s = %s' % (var.GetName(), var.GetValue()))

Note that this only returns the variables of this block; use
`SBFrame.GetVariables` to get the variables of all blocks that are in scope."
) lldb::SBBlock::GetVariables;

%feature("docstring",
"Writes a description of this block into the given `SBStream`."
) lldb::SBBlock::GetDescription;
