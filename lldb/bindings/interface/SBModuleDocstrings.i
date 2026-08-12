%feature("docstring",
"Represents an executable image and its associated object and symbol files.

The module is designed to be able to select a single slice of an
executable image as it would appear on disk and during program
execution.

You can retrieve SBModule from :py:class:`SBSymbolContext` , which in turn is available
from SBFrame.

SBModule supports symbol iteration, for example, ::

    for symbol in module:
        name = symbol.GetName()
        saddr = symbol.GetStartAddress()
        eaddr = symbol.GetEndAddress()

and rich comparison methods which allow the API program to use, ::

    if thisModule == thatModule:
        print('This module is the same as that module')

to test module equality.  A module also contains object file sections, namely
:py:class:`SBSection` .  SBModule supports section iteration through section_iter(), for
example, ::

    print('Number of sections: %d' % module.GetNumSections())
    for sec in module.section_iter():
        print(sec)

And to iterate the symbols within a SBSection, use symbol_in_section_iter(), ::

    # Iterates the text section and prints each symbols within each sub-section.
    for subsec in text_sec:
        print(INDENT + repr(subsec))
        for sym in exe_module.symbol_in_section_iter(subsec):
            print(INDENT2 + repr(sym))
            print(INDENT2 + 'symbol type: %s' % symbol_type_to_str(sym.GetType()))

produces this following output: ::

    [0x0000000100001780-0x0000000100001d5c) a.out.__TEXT.__text
        id = {0x00000004}, name = 'mask_access(MaskAction, unsigned int)', range = [0x00000001000017c0-0x0000000100001870)
        symbol type: code
        id = {0x00000008}, name = 'thread_func(void*)', range = [0x0000000100001870-0x00000001000019b0)
        symbol type: code
        id = {0x0000000c}, name = 'main', range = [0x00000001000019b0-0x0000000100001d5c)
        symbol type: code
        id = {0x00000023}, name = 'start', address = 0x0000000100001780
        symbol type: code
    [0x0000000100001d5c-0x0000000100001da4) a.out.__TEXT.__stubs
        id = {0x00000024}, name = '__stack_chk_fail', range = [0x0000000100001d5c-0x0000000100001d62)
        symbol type: trampoline
        id = {0x00000028}, name = 'exit', range = [0x0000000100001d62-0x0000000100001d68)
        symbol type: trampoline
        id = {0x00000029}, name = 'fflush', range = [0x0000000100001d68-0x0000000100001d6e)
        symbol type: trampoline
        id = {0x0000002a}, name = 'fgets', range = [0x0000000100001d6e-0x0000000100001d74)
        symbol type: trampoline
        id = {0x0000002b}, name = 'printf', range = [0x0000000100001d74-0x0000000100001d7a)
        symbol type: trampoline
        id = {0x0000002c}, name = 'pthread_create', range = [0x0000000100001d7a-0x0000000100001d80)
        symbol type: trampoline
        id = {0x0000002d}, name = 'pthread_join', range = [0x0000000100001d80-0x0000000100001d86)
        symbol type: trampoline
        id = {0x0000002e}, name = 'pthread_mutex_lock', range = [0x0000000100001d86-0x0000000100001d8c)
        symbol type: trampoline
        id = {0x0000002f}, name = 'pthread_mutex_unlock', range = [0x0000000100001d8c-0x0000000100001d92)
        symbol type: trampoline
        id = {0x00000030}, name = 'rand', range = [0x0000000100001d92-0x0000000100001d98)
        symbol type: trampoline
        id = {0x00000031}, name = 'strtoul', range = [0x0000000100001d98-0x0000000100001d9e)
        symbol type: trampoline
        id = {0x00000032}, name = 'usleep', range = [0x0000000100001d9e-0x0000000100001da4)
        symbol type: trampoline
    [0x0000000100001da4-0x0000000100001e2c) a.out.__TEXT.__stub_helper
    [0x0000000100001e2c-0x0000000100001f10) a.out.__TEXT.__cstring
    [0x0000000100001f10-0x0000000100001f68) a.out.__TEXT.__unwind_info
    [0x0000000100001f68-0x0000000100001ff8) a.out.__TEXT.__eh_frame
"
) lldb::SBModule;

%feature("docstring", "
    Check if the module is file backed.

    :return: ``True`` if the module is backed by an object file on disk,
        ``False`` if the module is backed by an object file in memory."
) lldb::SBModule::IsFileBacked;

%feature("docstring", "
    Get const accessor for the module file specification.

    This function returns the file for the module on the host system
    that is running LLDB. This can differ from the path on the
    platform since we might be doing remote debugging, see
    `SBModule.GetPlatformFileSpec`.

    :return: The file specification of this module.
    :rtype: SBFileSpec"
) lldb::SBModule::GetFileSpec;

%feature("docstring", "
    Get accessor for the module platform file specification.

    Platform file refers to the path of the module as it is known on
    the remote system on which it is being debugged. For local
    debugging this is always the same as Module::GetFileSpec(). But
    remote debugging might mention a file '/usr/lib/liba.dylib'
    which might be locally downloaded and cached. In this case the
    platform file could be something like:
    '/tmp/lldb/platform-cache/remote.host.computer/usr/lib/liba.dylib'
    The file could also be cached in a local developer kit directory.

    :return: The file specification of this module on the platform.
    :rtype: SBFileSpec"
) lldb::SBModule::GetPlatformFileSpec;

%feature("docstring", "Returns the UUID of the module as a Python string."
) lldb::SBModule::GetUUIDString;

%feature("docstring", "
    Find compile units related to this module and passed source
    file.

    :param sb_file_spec: A :py:class:`SBFileSpec` object that contains source
        file specification.
    :return: A :py:class:`SBSymbolContextList` that gets filled in with all of
        the symbol contexts for all the matches.
    :rtype: SBSymbolContextList"
) lldb::SBModule::FindCompileUnits;

%feature("docstring", "
    Find functions by name.

    :param name: The name of the function we are looking for.
    :param name_type_mask: A logical OR of one or more
        ``lldb.eFunctionNameType*`` bits that indicate what kind of names should
        be used when doing the lookup. Bits include fully qualified names, base
        names, C++ methods, or ObjC selectors.
    :return: A symbol context list that gets filled in with all of the matches.
    :rtype: SBSymbolContextList

    See `SBModule.FindSymbols` to search the symbol table instead of the debug
    information, and `SBTarget.FindFunctions` to search all modules of a
    target."
) lldb::SBModule::FindFunctions;

%feature("docstring", "
    Get all types matching type_mask from debug info in this
    module.

    :param type_mask: A bitfield that consists of one or more bits logically
        OR'ed together from the ``lldb.eTypeClass*`` enumerators. This allows
        you to request only structure types, or only class, struct
        and union types. Passing in ``lldb.eTypeClassAny`` will return
        all types found in the debug information for this module.
    :return: An `SBTypeList` of types in this module that match type_mask.
    :rtype: SBTypeList"
) lldb::SBModule::GetTypes;

%feature("docstring", "
    Find global and static variables by name.

    :param target: A valid `SBTarget` instance representing the debuggee.
    :param name: The name of the global or static variable we are looking for.
    :param max_matches: Allow the number of matches to be limited to
        max_matches.
    :return: A list of matched variables in an SBValueList.
    :rtype: SBValueList"
) lldb::SBModule::FindGlobalVariables;

%feature("docstring", "
    Find the first global (or static) variable by name.

    :param target: A valid `SBTarget` instance representing the debuggee.
    :param name: The name of the global or static variable we are looking for.
    :return: An SBValue that gets filled in with the found variable (if any).
    :rtype: SBValue"
) lldb::SBModule::FindFirstGlobalVariable;

%feature("docstring", "
    Returns the number of modules in the module cache. This is an
    implementation detail exposed for testing and should not be relied upon.

    :return: The number of modules in the module cache."
) lldb::SBModule::GetNumberAllocatedModules;

%feature("docstring", "
    Removes all modules which are no longer needed by any part of LLDB from
    the module cache.

    This is an implementation detail exposed for testing and should not be
    relied upon. Use SBDebugger::MemoryPressureDetected instead to reduce
    LLDB's memory consumption during execution.
") lldb::SBModule::GarbageCollectAllocatedModules;

%feature("docstring", "
    Returns whether this object refers to a module."
) lldb::SBModule::IsValid;

%feature("docstring", "
    Resets this object to an invalid module."
) lldb::SBModule::Clear;

%feature("docstring", "
    Sets the path of this module as it is known on the platform.

    See `SBModule.GetPlatformFileSpec`."
) lldb::SBModule::SetPlatformFileSpec;

%feature("docstring", "
    Returns the path this module is installed to on a remote platform.

    When debugging on a remote platform and an install path is set, the module is
    copied to that path before every launch. See
    `SBModule.SetRemoteInstallFileSpec` and `SBTarget.Install`."
) lldb::SBModule::GetRemoteInstallFileSpec;

%feature("docstring", "
    Sets the path this module should be installed to on a remote platform.

    If ``file`` is an absolute path the module is installed there, a relative
    path is resolved against the platform's working directory. See
    `SBModule.GetRemoteInstallFileSpec`."
) lldb::SBModule::SetRemoteInstallFileSpec;

%feature("docstring", "
    Returns the byte order of this module as one of the ``lldb.eByteOrder*``
    enumerators."
) lldb::SBModule::GetByteOrder;

%feature("docstring", "
    Returns the size in bytes of an address in this module, usually ``4`` or
    ``8``."
) lldb::SBModule::GetAddressByteSize;

%feature("docstring", "
    Returns the triple of this module, e.g. ``x86_64-apple-macosx``."
) lldb::SBModule::GetTriple;

%feature("docstring", "
    Returns the section with the given name as an `SBSection`.

    Only searches the top level sections; use `SBSection.FindSubSection` for
    subsections. Returns an invalid section if there is no such section::

        text = module.FindSection('__TEXT')
"
) lldb::SBModule::FindSection;

%feature("docstring", "
    Resolves a file address of this module into an `SBAddress`.

    File addresses are the addresses as they appear in the object file, see
    `SBAddress.GetFileAddress`. Returns an invalid address if no section of this
    module contains that address."
) lldb::SBModule::ResolveFileAddress;

%feature("docstring", "
    Returns the symbol context of an address in this module.

    ``resolve_scope`` is a bit mask of ``lldb.eSymbolContext*`` values that
    selects which parts to look up. See
    `SBTarget.ResolveSymbolContextForAddress`.

    :rtype: SBSymbolContext"
) lldb::SBModule::ResolveSymbolContextForAddress;

%feature("docstring", "
    Writes a description of this module into the given `SBStream`."
) lldb::SBModule::GetDescription;

%feature("docstring", "
    Returns the number of compile units of this module.

    Modules without debug information have no compile units. See
    `SBModule.GetCompileUnitAtIndex`."
) lldb::SBModule::GetNumCompileUnits;

%feature("docstring", "
    Returns the compile unit at the given index as an `SBCompileUnit`.

    In Python, ``module.compile_unit_iter()`` iterates over all compile units."
) lldb::SBModule::GetCompileUnitAtIndex;

%feature("docstring", "
    Returns the number of symbols in the symbol table of this module.

    In Python, iterating over a module yields all of its symbols."
) lldb::SBModule::GetNumSymbols;

%feature("docstring", "
    Returns the symbol at the given index as an `SBSymbol`."
) lldb::SBModule::GetSymbolAtIndex;

%feature("docstring", "
    Returns the first symbol with the given name as an `SBSymbol`.

    ``type`` optionally restricts the search to one of the
    ``lldb.eSymbolType*`` kinds. Returns an invalid symbol if nothing was
    found::

        symbol = module.FindSymbol('main')

    See `SBModule.FindSymbols` if several symbols can have that name."
) lldb::SBModule::FindSymbol;

%feature("docstring", "
    Returns all symbols with the given name as an `SBSymbolContextList`.

    ``type`` optionally restricts the search to one of the
    ``lldb.eSymbolType*`` kinds. Unlike `SBModule.FindFunctions` this searches
    the symbol table, so it also finds symbols without debug information."
) lldb::SBModule::FindSymbols;

%feature("docstring", "
    Returns the number of sections of this module.

    In Python, ``module.section_iter()`` iterates over all sections."
) lldb::SBModule::GetNumSections;

%feature("docstring", "
    Returns the section at the given index as an `SBSection`."
) lldb::SBModule::GetSectionAtIndex;

%feature("docstring", "
    Returns the first type with the given name as an `SBType`.

    Only searches the debug information of this module, see
    `SBTarget.FindFirstType` to search all modules of a target."
) lldb::SBModule::FindFirstType;

%feature("docstring", "
    Returns all types with the given name as an `SBTypeList`.

    See `SBModule.FindFirstType` if only one match is needed."
) lldb::SBModule::FindTypes;

%feature("docstring", "
    Returns the type with the given type ID as an `SBType`.

    Each symbol file reader assigns its own user IDs to types; for DWARF this is
    the offset of the type's debug information entry. This is mostly useful when
    debugging problems with debug information. Returns an invalid type if there
    is no type with that ID."
) lldb::SBModule::GetTypeByID;

%feature("docstring", "
    Returns an `SBType` for one of the language's builtin types.

    ``type`` is one of the ``lldb.eBasicType*`` enumerators, see
    `SBTarget.GetBasicType`."
) lldb::SBModule::GetBasicType;

%feature("docstring", "
    Returns the version numbers of this module.

    Many object files record a version, typically as major, minor and build
    numbers. In Python this takes no arguments and returns the numbers as a
    list::

        print(module.GetVersion())

    Returns an empty list if the object file has no version information."
) lldb::SBModule::GetVersion;

%feature("docstring", "
    Returns the file that holds the debug information of this module.

    Debug information can live in a separate file, for example in
    ``/usr/lib/liba.dylib.dSYM/`` for ``/usr/lib/liba.dylib``. Returns the
    module's own file if the debug information is not separate.

    :rtype: SBFileSpec"
) lldb::SBModule::GetSymbolFileSpec;

%feature("docstring", "
    Returns the separate debug info files of this module as an
    `SBModuleSpecList`.

    Separate debug info files are files that are referenced from debug info but
    that aren't the object file the symbol file parses: for split DWARF this is
    the ``.dwp`` file if it exists and the ``.dwo`` files otherwise, and for
    DWARF in ``.o`` files on Darwin the list of ``.o`` files if there is no dSYM.

    The list is empty if this module has no separate debug info files."
) lldb::SBModule::GetSeparateDebugInfoFiles;

%feature("docstring", "
    Returns the address of the object file header of this module as an
    `SBAddress`."
) lldb::SBModule::GetObjectFileHeaderAddress;

%feature("docstring", "
    Returns the entry point of this module as an `SBAddress`.

    This is where execution starts for an executable. Returns an invalid address
    for object files that have no entry point, such as shared libraries."
) lldb::SBModule::GetObjectFileEntryPointAddress;

%feature("docstring", "
    Returns the name of the object inside a larger file this module represents.

    Some files contain several objects, for example the members of a static
    archive or the architectures of a universal Mach-O file. Returns ``None``
    for modules that are a whole file."
) lldb::SBModule::GetObjectName;

%feature("docstring", "
    Returns the raw bytes of this module's UUID.

    Not useful from Python, where this is a raw pointer; use
    `SBModule.GetUUIDString` or the ``uuid`` property instead, which returns a
    standard ``uuid.UUID`` object."
) lldb::SBModule::GetUUIDBytes;
