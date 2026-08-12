%feature("docstring",
"Represents an executable image section.

SBSection supports iteration through its subsection, represented as SBSection
as well.  For example, ::

    for sec in exe_module.section_iter():
        if sec.GetName() == '__TEXT':
            print(sec)
            break
    print('Number of subsections: %d' % sec.GetNumSubSections())
    for subsec in sec:
        print(repr(subsec))

produces: ::

  [0x0000000100000000-0x0000000100002000) a.out.__TEXT
      Number of subsections: 6
      [0x0000000100001780-0x0000000100001d5c) a.out.__TEXT.__text
      [0x0000000100001d5c-0x0000000100001da4) a.out.__TEXT.__stubs
      [0x0000000100001da4-0x0000000100001e2c) a.out.__TEXT.__stub_helper
      [0x0000000100001e2c-0x0000000100001f10) a.out.__TEXT.__cstring
      [0x0000000100001f10-0x0000000100001f68) a.out.__TEXT.__unwind_info
      [0x0000000100001f68-0x0000000100001ff8) a.out.__TEXT.__eh_frame

Sections describe how an object file is laid out: they say which parts of the
file contain code, data or debug information and at which addresses those parts
end up in memory. They are obtained from `SBModule.GetSectionAtIndex`,
`SBModule.FindSection` or from an address with `SBAddress.GetSection`, and they
are what makes an `SBAddress` stay valid when a library is loaded at a different
address than the one in its object file.

See also :py:class:`SBModule` and :py:class:`SBAddress`."
) lldb::SBSection;

%feature("docstring",
"Returns whether this object refers to a section."
) lldb::SBSection::IsValid;

%feature("docstring",
"Returns the name of this section, e.g. ``__TEXT`` or ``.debug_info``."
) lldb::SBSection::GetName;

%feature("docstring",
"Returns the section this section is a part of.

Returns an invalid section for top level sections. See
`SBSection.GetSubSectionAtIndex` for the other direction."
) lldb::SBSection::GetParent;

%feature("docstring",
"Returns the subsection with the given name.

Returns an invalid section if this section has no such subsection::

    text = module.FindSection('__TEXT').FindSubSection('__text')
"
) lldb::SBSection::FindSubSection;

%feature("docstring",
"Returns the number of subsections of this section.

In Python, iterating over a section yields its subsections."
) lldb::SBSection::GetNumSubSections;

%feature("docstring",
"Returns the subsection at the given index."
) lldb::SBSection::GetSubSectionAtIndex;

%feature("docstring",
"Returns the address of this section as it appears in the object file.

See `SBSection.GetLoadAddress` for where the section ended up in memory."
) lldb::SBSection::GetFileAddress;

%feature("docstring",
"Returns the address this section is loaded at in the given target.

Returns ``lldb.LLDB_INVALID_ADDRESS`` if the section is not loaded, for example
because the process is not running yet or the section is not mapped into
memory."
) lldb::SBSection::GetLoadAddress;

%feature("docstring",
"Returns the size in bytes this section occupies in memory.

This can differ from `SBSection.GetFileByteSize`, for example for sections such
as ``__bss`` that occupy memory but no space in the file."
) lldb::SBSection::GetByteSize;

%feature("docstring",
"Returns the offset of this section's contents inside the object file."
) lldb::SBSection::GetFileOffset;

%feature("docstring",
"Returns the size in bytes this section occupies in the object file."
) lldb::SBSection::GetFileByteSize;

%feature("docstring",
"Returns the contents of this section as an `SBData`.

The overload that takes an offset and a size only reads that part of the
section. The data is read from the object file, so this works without a running
process::

    data = module.FindSection('__TEXT').GetSectionData()
    print(len(data))
"
) lldb::SBSection::GetSectionData;

%feature("docstring",
"Returns what kind of section this is as one of the ``lldb.eSectionType*``
enumerators.

This tells for example code sections (``lldb.eSectionTypeCode``) apart from data
and DWARF sections."
) lldb::SBSection::GetSectionType;

%feature("docstring",
"Returns the memory permissions of this section.

The result is a bit mask of the ``lldb.ePermissions*`` values::

    if section.GetPermissions() & lldb.ePermissionsExecutable:
        print('%s is executable' % section.GetName())
"
) lldb::SBSection::GetPermissions;

%feature("docstring",
"Returns the alignment of this section in bytes.

The result is always a power of two, e.g. ``16`` for a section that is aligned to
16 bytes. Returns ``0`` for an invalid section."
) lldb::SBSection::GetAlignment;

%feature("docstring",
"Writes a description of this section into the given `SBStream`."
) lldb::SBSection::GetDescription;

%feature("docstring", "Deprecated. Always returns 1."
) lldb::SBSection::GetTargetByteSize;
