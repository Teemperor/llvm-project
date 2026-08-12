%feature("docstring",
"A section + offset based address class.

The SBAddress class allows addresses to be relative to a section
that can move during runtime due to images (executables, shared
libraries, bundles, frameworks) being loaded at different
addresses than the addresses found in the object file that
represents them on disk. There are currently two types of addresses
for a section:

* file addresses
* load addresses

File addresses represents the virtual addresses that are in the 'on
disk' object files. These virtual addresses are converted to be
relative to unique sections scoped to the object file so that
when/if the addresses slide when the images are loaded/unloaded
in memory, we can easily track these changes without having to
update every object (compile unit ranges, line tables, function
address ranges, lexical block and inlined subroutine address
ranges, global and static variables) each time an image is loaded or
unloaded.

Load addresses represents the virtual addresses where each section
ends up getting loaded at runtime. Before executing a program, it
is common for all of the load addresses to be unresolved. When a
DynamicLoader plug-in receives notification that shared libraries
have been loaded/unloaded, the load addresses of the main executable
and any images (shared libraries) will be  resolved/unresolved. When
this happens, breakpoints that are in one of these sections can be
set/cleared.

Addresses are produced by many API functions, for example
`SBFrame.GetPCAddress`, `SBSymbol.GetStartAddress`,
`SBBreakpointLocation.GetAddress` and `SBTarget.ResolveLoadAddress`. Once an
address is resolved, the debug information and symbols at that address can be
looked up::

    addr = target.ResolveLoadAddress(pc)
    print('%s + %d in %s' % (addr.GetSymbol().GetName(), addr.GetOffset(),
                             addr.GetModule().GetFileSpec().GetFilename()))
    print('%s:%d' % (addr.GetLineEntry().GetFileSpec(), addr.GetLineEntry().GetLine()))

See also :py:class:`SBSymbolContext`, which holds all of the above information
at once, and :py:class:`SBAddressRange` for a range of addresses."
) lldb::SBAddress;

%feature("docstring", "
    Returns whether this address refers to a section of a module.

    Note that an address can be valid but still not resolve to any debug
    information or symbol."
) lldb::SBAddress::IsValid;

%feature("docstring", "
    Resets this object to an invalid address."
) lldb::SBAddress::Clear;

%feature("docstring", "
    Returns the file address as an integer.

    The file address is the address as it appears in the object file on disk,
    which is what tools such as ``nm`` or ``objdump`` print. Returns
    ``lldb.LLDB_INVALID_ADDRESS`` if this address has no section."
) lldb::SBAddress::GetFileAddress;

%feature("docstring", "
    Returns the load address in the given target as an integer.

    The load address is where the section of this address ended up in memory.
    Returns ``lldb.LLDB_INVALID_ADDRESS`` if the section is not loaded, which is
    the case before the program is running."
) lldb::SBAddress::GetLoadAddress;

%feature("docstring", "
    Sets this address to the given offset into the given `SBSection`."
) lldb::SBAddress::SetAddress;

%feature("docstring", "
    Sets this address by resolving a load address in the given target.

    This finds the section that contains ``load_addr`` and makes this address
    relative to it, so that the debug information at that address can be looked
    up. If no section contains the address, this address becomes a plain address
    without a section."
) lldb::SBAddress::SetLoadAddress;

%feature("docstring", "
    Adds the given offset to this address.

    Returns ``False`` if the resulting address is not inside the section of this
    address."
) lldb::SBAddress::OffsetAddress;

%feature("docstring", "
    Writes a description of this address into the given `SBStream`."
) lldb::SBAddress::GetDescription;

%feature("docstring", "
    Returns the `SBSection` this address is relative to.

    Returns an invalid section if this address doesn't belong to a section, for
    example because it points into the stack or the heap."
) lldb::SBAddress::GetSection;

%feature("docstring", "
    Returns the offset of this address inside its section."
) lldb::SBAddress::GetOffset;

%feature("docstring", "
    Returns the `SBCompileUnit` of the code at this address.

    Invalid if there is no debug information for this address, see
    `SBAddress.GetSymbolContext`."
) lldb::SBAddress::GetCompileUnit;

%feature("docstring", "
    Returns the `SBFunction` at this address.

    Invalid if there is no debug information for this address; use
    `SBAddress.GetSymbol` in that case."
) lldb::SBAddress::GetFunction;

%feature("docstring", "
    Returns the innermost `SBBlock` at this address."
) lldb::SBAddress::GetBlock;

%feature("docstring", "
    Returns the `SBSymbol` at this address.

    Unlike `SBAddress.GetFunction` this also works without debug information as
    it uses the symbol table of the module."
) lldb::SBAddress::GetSymbol;

%feature("docstring", "
    Returns the `SBLineEntry` at this address, i.e. the source file and line the
    code at this address belongs to."
) lldb::SBAddress::GetLineEntry;

%feature("docstring", "
    Create an address by resolving a load address using the supplied target.")
lldb::SBAddress::SBAddress;

%feature("docstring", "
    GetSymbolContext() and the following can lookup symbol information for a given address.
    An address might refer to code or data from an existing module, or it
    might refer to something on the stack or heap. The following functions
    will only return valid values if the address has been resolved to a code
    or data address using :py:class:`SBAddress.SetLoadAddress' or
    :py:class:`SBTarget.ResolveLoadAddress`.") lldb::SBAddress::GetSymbolContext;

%feature("docstring", "
    GetModule() and the following grab individual objects for a given address and
    are less efficient if you want more than one symbol related objects.
    Use :py:class:`SBAddress.GetSymbolContext` or
    :py:class:`SBTarget.ResolveSymbolContextForAddress` when you want multiple
    debug symbol related objects for an address.
    One or more bits from the SymbolContextItem enumerations can be logically
    OR'ed together to more efficiently retrieve multiple symbol objects.")
lldb::SBAddress::GetModule;
