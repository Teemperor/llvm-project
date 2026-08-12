%feature("docstring",
"Represents a compilation unit, or compiled source file.

SBCompileUnit supports line entry iteration. For example,::

    # Now get the SBSymbolContext from this frame.  We want everything. :-)
    context = frame0.GetSymbolContext(lldb.eSymbolContextEverything)
    ...

    compileUnit = context.GetCompileUnit()

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

See also :py:class:`SBSymbolContext` and :py:class:`SBLineEntry`"
) lldb::SBCompileUnit;

%feature("docstring", "
     Get the index for a provided line entry in this compile unit.

     :param line_entry: The `SBLineEntry` object for which we are looking for
        the index.
     :param exact: An optional boolean defaulting to false that ensures that the
        provided line entry has a perfect match in the compile unit.
     :return: The index of the user-provided line entry. ``lldb.UINT32_MAX`` if
        the line entry was not found in the compile unit.

     The overload that takes a ``start_idx``, a ``line`` and an `SBFileSpec`
     searches for the first line entry at or after ``start_idx`` that matches
     that line of that file, which is how a breakpoint by file and line finds
     its locations::

         index = compile_unit.FindLineEntryIndex(0, 42, lldb.SBFileSpec('main.c'))
         line_entry = compile_unit.GetLineEntryAtIndex(index)
") lldb::SBCompileUnit::FindLineEntryIndex;

%feature("docstring", "
     Get all types matching type_mask from debug info in this
     compile unit.

     :param type_mask: A bitfield that consists of one or more bits logically
        OR'ed together from the ``lldb.eTypeClass*`` enumerators. This allows
        you to request only structure types, or only class, struct
        and union types. Passing in ``lldb.eTypeClassAny`` will return
        all types found in the debug information for this compile
        unit.
     :return: An `SBTypeList` of types in this compile unit that match
        type_mask.
     :rtype: SBTypeList"
) lldb::SBCompileUnit::GetTypes;

%feature("docstring",
"Returns whether this object refers to a compile unit."
) lldb::SBCompileUnit::IsValid;

%feature("docstring",
"Returns the source file this compile unit was compiled from as an
`SBFileSpec`."
) lldb::SBCompileUnit::GetFileSpec;

%feature("docstring",
"Returns the number of line table entries of this compile unit.

See `SBCompileUnit.GetLineEntryAtIndex`; in Python, iterating over a compile
unit yields all of its line entries."
) lldb::SBCompileUnit::GetNumLineEntries;

%feature("docstring",
"Returns the line table entry at the given index as an `SBLineEntry`.

The entries are sorted by address, not by line number, so the same source line
can appear several times."
) lldb::SBCompileUnit::GetLineEntryAtIndex;

%feature("docstring",
"Returns the number of support files of this compile unit.

Support files are the files the code of this compile unit came from: the main
source file plus every header and inlined file that contributed code. See
`SBCompileUnit.GetSupportFileAtIndex`."
) lldb::SBCompileUnit::GetNumSupportFiles;

%feature("docstring",
"Returns the support file at the given index as an `SBFileSpec`.

Index ``0`` is the primary source file of this compile unit, which is also what
`SBCompileUnit.GetFileSpec` returns."
) lldb::SBCompileUnit::GetSupportFileAtIndex;

%feature("docstring",
"Returns the index of the given support file.

The search starts at ``start_idx`` and ``full`` decides whether the whole path
has to match or just the file name. Returns ``lldb.UINT32_MAX`` if the file was
not found."
) lldb::SBCompileUnit::FindSupportFileIndex;

%feature("docstring",
"Returns the language of this compile unit as one of the ``lldb.eLanguageType*``
enumerators."
) lldb::SBCompileUnit::GetLanguage;

%feature("docstring",
"Writes a description of this compile unit into the given `SBStream`."
) lldb::SBCompileUnit::GetDescription;
