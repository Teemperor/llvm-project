%feature("docstring",
"Represents a program that can be or is being debugged.

A target is the central object of a debug session. It is created from an
`SBDebugger` (`SBDebugger.CreateTarget`) and it holds everything that belongs
to one program: its modules (`SBTarget.GetModuleAtIndex`), its breakpoints and
watchpoints, its symbols and types (`SBTarget.FindFirstType`,
`SBTarget.FindFunctions`) and, once the program is running, its `SBProcess`::

    debugger = lldb.SBDebugger.Create()
    target = debugger.CreateTarget('/path/to/a.out')
    target.BreakpointCreateByName('main')
    process = target.LaunchSimple(None, None, os.getcwd())

Note that a target exists without a running process, which makes it possible to
set breakpoints, look up types and read memory from the object files before
launching (`SBTarget.ReadMemory`, `SBTarget.ResolveFileAddress`).

SBTarget supports module, breakpoint, and watchpoint iterations. For example, ::

    for m in target.module_iter():
        print(m)

produces: ::

    (x86_64) /Volumes/data/lldb/svn/trunk/test/python_api/lldbutil/iter/a.out
    (x86_64) /usr/lib/dyld
    (x86_64) /usr/lib/libstdc++.6.dylib
    (x86_64) /usr/lib/libSystem.B.dylib
    (x86_64) /usr/lib/system/libmathCommon.A.dylib
    (x86_64) /usr/lib/libSystem.B.dylib(__commpage)

and, ::

    for b in target.breakpoint_iter():
        print(b)

produces: ::

    SBBreakpoint: id = 1, file ='main.cpp', line = 66, locations = 1
    SBBreakpoint: id = 2, file ='main.cpp', line = 85, locations = 1

and, ::

    for wp_loc in target.watchpoint_iter():
        print(wp_loc)

produces: ::

    Watchpoint 1: addr = 0x1034ca048, size = 4, state = enabled, type = rw
        declare @ '/Volumes/data/lldb/svn/trunk/test/python_api/watchpoint/main.c:12'
        hit_count = 2     ignore_count = 0

See also :py:class:`SBDebugger`, :py:class:`SBProcess`, :py:class:`SBModule`
and :py:class:`SBBreakpoint`."
) lldb::SBTarget;

%feature("docstring", "
    Returns whether this object refers to a target.

    Targets stay valid until they are deleted with
    `SBDebugger.DeleteTarget`."
) lldb::SBTarget::IsValid;

%feature("docstring", "
    Returns whether the given `SBEvent` is a target event.

    Target events are sent when modules are added to or removed from a target,
    see `SBTarget.GetNumModulesFromEvent`."
) lldb::SBTarget::EventIsTargetEvent;

%feature("docstring", "
    Returns the `SBTarget` a target event refers to."
) lldb::SBTarget::GetTargetFromEvent;

%feature("docstring", "
    Returns the target that was created, if the event is a target creation
    event."
) lldb::SBTarget::GetCreatedTargetFromEvent;

%feature("docstring", "
    Returns how many modules a modules-changed target event refers to.

    Use `SBTarget.GetModuleAtIndexFromEvent` to get the modules themselves."
) lldb::SBTarget::GetNumModulesFromEvent;

%feature("docstring", "
    Returns one of the modules a modules-changed target event refers to.

    See `SBTarget.GetNumModulesFromEvent`."
) lldb::SBTarget::GetModuleAtIndexFromEvent;

%feature("docstring", "
    Returns the name of the broadcaster class that sends target events
    (``lldb.target``).

    Pass this to `SBListener.StartListeningForEventClass` to receive target
    events."
) lldb::SBTarget::GetBroadcasterClassName;

%feature("docstring", "
    Returns the `SBProcess` of this target.

    Returns an invalid process if the target has no process, i.e. if it was
    neither launched nor attached to anything. See `SBTarget.Launch`,
    `SBTarget.Attach` and `SBTarget.LoadCore` for creating one."
) lldb::SBTarget::GetProcess;

%feature("docstring", "
    Enables or disables the collection of debug session statistics.

    See `SBTarget.GetStatistics` for reading the collected data."
) lldb::SBTarget::SetCollectingStats;

%feature("docstring", "
    Returns whether debug session statistics are being collected.

    See `SBTarget.SetCollectingStats`."
) lldb::SBTarget::GetCollectingStats;

%feature("docstring", "
    Returns statistics about this debug session as `SBStructuredData`.

    This is the data the ``statistics dump`` command prints: how long symbol
    parsing took, how many modules have debug information, breakpoint
    resolution times and more. The overload that takes an
    `SBStatisticsOptions` controls how much detail is included::

        stats = target.GetStatistics()
        stream = lldb.SBStream()
        stats.GetAsJSON(stream)
        print(stream.GetData())
"
) lldb::SBTarget::GetStatistics;

%feature("docstring", "
    Discards the statistics that were collected so far, see
    `SBTarget.GetStatistics`."
) lldb::SBTarget::ResetStatistics;

%feature("docstring", "
    Return the platform object associated with the target.

    After return, the platform object should be checked for
    validity.

    The platform describes the system the program runs on and is used for
    example to transfer files to a remote system, see `SBPlatform`.

    :return: A platform object.
    :rtype: SBPlatform"
) lldb::SBTarget::GetPlatform;

%feature("docstring", "
    Returns the environment that will be used when launching a process.

    The returned `SBEnvironment` is a copy, so changing it does not change the
    target's environment; use `SBLaunchInfo.SetEnvironment` to launch with a
    modified environment."
) lldb::SBTarget::GetEnvironment;

%feature("docstring", "
    Install any binaries that need to be installed.

    This function does nothing when debugging on the host system.
    When connected to remote platforms, the target's main executable
    and any modules that have their install path set will be
    installed on the remote platform. If the main executable doesn't
    have an install location set, it will be installed in the remote
    platform's working directory.

    :return: An error describing anything that went wrong during installation.
    :rtype: SBError"
) lldb::SBTarget::Install;

%feature("docstring", "
    Launch a new process.

    Launch a new process by spawning a new process using the
    target object's executable module's file as the file to launch.
    Arguments are given in argv, and the environment variables
    are in envp. Standard input and output files can be
    optionally re-directed to stdin_path, stdout_path, and
    stderr_path.

    :param listener: An optional `SBListener` that will receive all process
        events. If listener is valid then listener will listen to all
        process events. If not valid, then this target's debugger
        (`SBTarget.GetDebugger`) will listen to all process events.
    :param argv: The argument array.
    :param envp: The environment array.
    :param launch_flags: Some launch options specified by logical OR'ing
        ``lldb.eLaunchFlag*`` enumeration values together.
    :param stdin_path: The path to use when re-directing the STDIN of the new
        process. If all stdXX_path arguments are None, a pseudo
        terminal will be used.
    :param stdout_path: The path to use when re-directing the STDOUT of the new
        process. If all stdXX_path arguments are None, a pseudo
        terminal will be used.
    :param stderr_path: The path to use when re-directing the STDERR of the new
        process. If all stdXX_path arguments are None, a pseudo
        terminal will be used.
    :param working_directory: The working directory to have the child process
        run in.
    :param stop_at_entry: If false do not stop the inferior at the entry point.
    :param error: An `SBError`. Contains the reason if there is some failure.
    :return: A process object for the newly created process.
    :rtype: SBProcess

    For example, ::

        process = target.Launch(self.dbg.GetListener(), None, None,
                                None, '/tmp/stdout.txt', None,
                                None, 0, False, error)

    launches a new process by passing nothing for both the args and the envs
    and redirect the standard output of the inferior to the /tmp/stdout.txt
    file. It does not specify a working directory so that the debug server
    will use its idea of what the current working directory is for the
    inferior. Also, we ask the debugger not to stop the inferior at the
    entry point. If no breakpoint is specified for the inferior, it should
    run to completion if no user interaction is required.

    The overload that takes an `SBLaunchInfo` gives access to all launch
    options, and `SBTarget.LaunchSimple` is a convenient shortcut for the
    common case."
) lldb::SBTarget::Launch;

%feature("docstring", "
    Launch a new process with sensible defaults.

    :param argv: The argument array.
    :param envp: The environment array.
    :param working_directory: The working directory to have the child process run in
    :return: The newly created process.
    :rtype: SBProcess

    A pseudo terminal will be used as stdin/stdout/stderr.
    No launch flags are passed and the target's debuger is used as a listener.

    For example, ::

        process = target.LaunchSimple(['X', 'Y', 'Z'], None, os.getcwd())

    launches a new process by passing 'X', 'Y', 'Z' as the args to the
    executable.

    See `SBTarget.Launch` if more control over the launch is needed."
) lldb::SBTarget::LaunchSimple;

%feature("docstring", "
    Load a core file

    :param core_file: File path of the core dump.
    :param error: An `SBError` explaining what went wrong if the operation
        fails. (Optional)
    :return: A process object for the newly created core file.
    :rtype: SBProcess

    For example, ::

        process = target.LoadCore('./a.out.core')

    loads a new core file and returns the process object. The resulting process
    cannot be resumed, see `SBProcess.IsLiveDebugSession`."
) lldb::SBTarget::LoadCore;

%feature("docstring", "
    Attaches to a running process.

    ``attach_info`` is an `SBAttachInfo` describing which process to attach to
    and how::

        attach_info = lldb.SBAttachInfo(pid)
        error = lldb.SBError()
        process = target.Attach(attach_info, error)

    See `SBTarget.AttachToProcessWithID` and
    `SBTarget.AttachToProcessWithName` for simpler variants."
) lldb::SBTarget::Attach;

%feature("docstring", "
    Attach to process with pid.

    :param listener: An optional `SBListener` that will receive all process
        events. If listener is valid then listener will listen to all
        process events. If not valid, then this target's debugger
        (`SBTarget.GetDebugger`) will listen to all process events.
    :param pid: The process ID to attach to.
    :param error: An `SBError` explaining what went wrong if attach fails.
    :return: A process object for the attached process.
    :rtype: SBProcess"
) lldb::SBTarget::AttachToProcessWithID;

%feature("docstring", "
    Attach to process with name.

    :param listener: An optional `SBListener` that will receive all process
        events. If listener is valid then listener will listen to all
        process events. If not valid, then this target's debugger
        (`SBTarget.GetDebugger`) will listen to all process events.
    :param name: Basename of process to attach to.
    :param wait_for: If true wait for a new instance of 'name' to be launched.
    :param error: An `SBError` explaining what went wrong if attach fails.
    :return: A process object for the attached process.
    :rtype: SBProcess"
) lldb::SBTarget::AttachToProcessWithName;

%feature("docstring", "
    Connect to a remote debug server with url.

    :param listener: An optional `SBListener` that will receive all process
        events. If listener is valid then listener will listen to all
        process events. If not valid, then this target's debugger
        (`SBTarget.GetDebugger`) will listen to all process events.
    :param url: The url to connect to, e.g., 'connect://localhost:12345'.
    :param plugin_name: The plugin name to be used; can be None.
    :param error: An `SBError` explaining what went wrong if the connect fails.
    :return: A process object for the connected process.
    :rtype: SBProcess"
) lldb::SBTarget::ConnectRemote;

%feature("docstring", "
    Returns the main executable of this target as an `SBFileSpec`."
) lldb::SBTarget::GetExecutable;

%feature("docstring", "
    Append the path mapping (from -> to) to the target's paths mapping list.

    Path mappings are used to find source files and modules whose paths changed
    since they were built, for instance when debugging a binary that was
    compiled on a different machine. This is the same as the
    ``target.source-map`` setting::

        target.AppendImageSearchPath('/build/machine/src', '/local/src', lldb.SBError())
"
) lldb::SBTarget::AppendImageSearchPath;

%feature("docstring", "
    Adds a module to this target.

    Modules are usually added automatically when a process loads a shared
    library, so this is mostly useful to make LLDB aware of a binary it can't
    find on its own. The overloads either take an existing `SBModule`, a path
    together with a triple and an optional UUID and symbol file, or an
    `SBModuleSpec`::

        module = target.AddModule('/path/to/lib.so', None, None)

    Note that adding a module does not tell LLDB where it is loaded in memory,
    see `SBTarget.SetModuleLoadAddress` for that."
) lldb::SBTarget::AddModule;

%feature("docstring", "
    Returns the number of modules of this target.

    See `SBTarget.GetModuleAtIndex`; in Python ``target.module_iter()``
    iterates over all modules."
) lldb::SBTarget::GetNumModules;

%feature("docstring", "
    Returns the module at the given index as an `SBModule`.

    Module ``0`` is usually the main executable. Returns an invalid module if
    the index is out of bounds."
) lldb::SBTarget::GetModuleAtIndex;

%feature("docstring", "
    Removes a module from this target and returns whether that succeeded."
) lldb::SBTarget::RemoveModule;

%feature("docstring", "
    Returns the `SBDebugger` this target belongs to."
) lldb::SBTarget::GetDebugger;

%feature("docstring", "
    Returns the module that matches the given `SBFileSpec` or `SBModuleSpec`.

    Returns an invalid `SBModule` if this target has no such module::

        module = target.FindModule(lldb.SBFileSpec('libfoo.dylib'))
"
) lldb::SBTarget::FindModule;

%feature("docstring", "
    Find compile units related to this target and passed source
    file.

    :param sb_file_spec: A :py:class:`lldb::SBFileSpec` object that contains source file
        specification.
    :return: The symbol contexts for all the matches.
    :rtype: SBSymbolContextList"
) lldb::SBTarget::FindCompileUnits;

%feature("docstring", "
    Returns the byte order of this target as one of the ``lldb.eByteOrder*``
    enumerators."
) lldb::SBTarget::GetByteOrder;

%feature("docstring", "
    Returns the size in bytes of an address in this target, usually ``4`` or
    ``8``."
) lldb::SBTarget::GetAddressByteSize;

%feature("docstring", "
    Returns the target triple, e.g. ``x86_64-apple-macosx15.0.0``.

    See `SBTarget.GetArchName` for just the architecture and
    `SBTarget.GetABIName` for the ABI."
) lldb::SBTarget::GetTriple;

%feature("docstring", "
    Returns the name of the architecture of this target, e.g. ``x86_64`` or
    ``arm64``."
) lldb::SBTarget::GetArchName;

%feature("docstring", "
    Returns the name of the ABI this target uses, if the architecture has
    several."
) lldb::SBTarget::GetABIName;

%feature("docstring", "
    Returns the label of this target.

    Labels are user-provided names that make targets easier to tell apart in
    the ``target list`` output. See `SBTarget.SetLabel`."
) lldb::SBTarget::GetLabel;

%feature("docstring", "
    Sets the label of this target and returns an `SBError`.

    Labels have to be unique within a debugger and must not be a number (which
    would be ambiguous with a target index)."
) lldb::SBTarget::SetLabel;

%feature("docstring", "
    Returns an ID that is unique across all targets of all debuggers in this
    process.

    Returns ``lldb.LLDB_INVALID_GLOBALLY_UNIQUE_TARGET_ID`` for an invalid
    target."
) lldb::SBTarget::GetGloballyUniqueID;

%feature("docstring", "
    Returns the session name of this target, if it has one.

    The session name is a meaningful name that IDEs and other tools can display
    to help a user identify where a target came from."
) lldb::SBTarget::GetTargetSessionName;

%feature("docstring", "
    Returns the size in bytes of the smallest instruction of this
    architecture."
) lldb::SBTarget::GetMinimumOpcodeByteSize;

%feature("docstring", "
    Returns the size in bytes of the largest instruction of this
    architecture."
) lldb::SBTarget::GetMaximumOpcodeByteSize;

%feature("docstring", "Deprecated. Always returns 1."
) lldb::SBTarget::GetDataByteSize;

%feature("docstring", "Deprecated. Always returns 1."
) lldb::SBTarget::GetCodeByteSize;

%feature("docstring", "
    Returns the value of the ``target.max-children-count`` setting.

    Use it to limit how many children of large data structures a tool
    displays."
) lldb::SBTarget::GetMaximumNumberOfChildrenToDisplay;

%feature("docstring", "
    Sets the load address of a section of a module.

    Use this to tell LLDB where a module is loaded when there is no dynamic
    loader that could report it, for example when debugging firmware. Returns
    an `SBError` describing any failure. See
    `SBTarget.SetModuleLoadAddress` to move a whole module at once."
) lldb::SBTarget::SetSectionLoadAddress;

%feature("docstring", "
    Removes the load address of a section, undoing
    `SBTarget.SetSectionLoadAddress`."
) lldb::SBTarget::ClearSectionLoadAddress;

%feature("docstring", "
    Sets the load address of all sections of a module by giving a slide.

    ``sections_offset`` is added to the file addresses of every section of the
    module. This is what the ``target modules load --slide`` command does::

        error = target.SetModuleLoadAddress(module, 0x10000)

    Returns an `SBError` describing any failure."
) lldb::SBTarget::SetModuleLoadAddress;

%feature("docstring", "
    Removes the load addresses of all sections of a module, undoing
    `SBTarget.SetModuleLoadAddress`."
) lldb::SBTarget::ClearModuleLoadAddress;

%feature("docstring", "
    Find functions by name.

    :param name: The name of the function we are looking for.

    :param name_type_mask:
        A logical OR of one or more FunctionNameType enum bits that
        indicate what kind of names should be used when doing the
        lookup. Bits include fully qualified names, base names,
        C++ methods, or ObjC selectors.
        See FunctionNameType for more details.

    :return:
        A lldb::SBSymbolContextList that gets filled in with all of
        the symbol contexts for all the matches.

    For example, ::

        for context in target.FindFunctions('main'):
            print(context.GetFunction().GetName())

    See `SBTarget.FindSymbols` to search the symbol table instead of the debug
    information, and `SBTarget.FindGlobalFunctions` to search by name
    fragment."
) lldb::SBTarget::FindFunctions;

%feature("docstring", "
    Find global and static variables by name.

    :param name: The name of the global or static variable we are looking for.
    :param max_matches: Allow the number of matches to be limited to
        max_matches.
    :param matchtype: One of the ``lldb.eMatchType*`` enumerators, which allows
        matching by regular expression or by name fragment instead of by exact
        name.
    :return: A list of matched variables in an SBValueList.
    :rtype: SBValueList"
) lldb::SBTarget::FindGlobalVariables;

 %feature("docstring", "
    Find the first global (or static) variable by name.

    :param name: The name of the global or static variable we are looking for.
    :return: An SBValue that gets filled in with the found variable (if any).
    :rtype: SBValue"
) lldb::SBTarget::FindFirstGlobalVariable;

%feature("docstring", "
    Find global functions by their name with pattern matching.

    :param name: The name or name fragment to search for.
    :param max_matches: The maximum number of matches to return.
    :param matchtype: One of the ``lldb.eMatchType*`` enumerators, selecting
        whether ``name`` is a full name, a fragment or a regular expression.
    :rtype: SBSymbolContextList

    See `SBTarget.FindFunctions` for a search by exact name."
) lldb::SBTarget::FindGlobalFunctions;

%feature("docstring", "
    Resets this object to an invalid target."
) lldb::SBTarget::Clear;

%feature("docstring", "
    Resolve a current file address into a section offset address.

    File addresses are the addresses as they appear in the object file, which
    is what tools like ``nm`` or ``objdump`` print. The returned `SBAddress` is
    section-relative and can be turned into a load address with
    `SBAddress.GetLoadAddress` once the module is loaded.

    :param file_addr: The file address to resolve.
    :return: An SBAddress which is valid if the address could be resolved to a
        section of a module in this target.
    :rtype: SBAddress"
) lldb::SBTarget::ResolveFileAddress;

%feature("docstring", "
    Resolve a load address into a section offset address.

    ``vm_addr`` is an address in the address space of the running process. The
    returned `SBAddress` can be used to look up the symbol, function or line
    entry at that address::

        addr = target.ResolveLoadAddress(pc)
        print(addr.GetSymbol().GetName())

    :rtype: SBAddress"
) lldb::SBTarget::ResolveLoadAddress;

%feature("docstring", "
    Resolve a load address as it was at a given stop.

    Like `SBTarget.ResolveLoadAddress`, but resolves the address using the
    sections that were loaded at the given stop ID (see `SBProcess.GetStopID`).
    Pass ``lldb.UINT32_MAX`` to use the currently loaded sections.

    :rtype: SBAddress"
) lldb::SBTarget::ResolvePastLoadAddress;

%feature("docstring", "
    Returns the symbol context of the given address.

    ``resolve_scope`` is a bit mask of ``lldb.eSymbolContext*`` values that
    selects which parts of the context to look up; looking up fewer parts is
    cheaper::

        sc = target.ResolveSymbolContextForAddress(addr, lldb.eSymbolContextEverything)
        print('%s at %s' % (sc.GetFunction().GetName(), sc.GetLineEntry()))

    :rtype: SBSymbolContext"
) lldb::SBTarget::ResolveSymbolContextForAddress;

%feature("docstring", "
    Read target memory. If a target process is running then memory
    is read from here. Otherwise the memory is read from the object
    files. For a target whose bytes are sized as a multiple of host
    bytes, the data read back will preserve the target's byte order.

    :param addr: An `SBAddress` to read from.
    :param buf: In Python this parameter is replaced by the number of bytes to
        read, and the bytes that were read are returned as a ``bytes`` object.
    :param error: An `SBError` that is filled in if the memory read fails.
    :return: The bytes that were read.

    For example, ::

        error = lldb.SBError()
        data = target.ReadMemory(addr, 8, error)

    See `SBProcess.ReadMemory` for reading from a running process by plain
    address."
) lldb::SBTarget::ReadMemory;

%feature("docstring", "
    Adds a breakpoint override implemented by a Python class.

    A breakpoint override can change how breakpoints of the kinds selected by
    ``type_mask`` (a logical OR of ``lldb.eBreakpointResolver*`` values) are
    resolved. Returns the ID of the new override, which can be passed to
    `SBTarget.RemoveBreakpointOverride`, or ``lldb.LLDB_INVALID_INDEX64`` on
    error, in which case ``status`` explains why."
) lldb::SBTarget::AddBreakpointOverride;

%feature("docstring", "
    Removes a breakpoint override that was added with
    `SBTarget.AddBreakpointOverride`.

    Returns whether an override with that ID existed."
) lldb::SBTarget::RemoveBreakpointOverride;

%feature("docstring", "
    Creates a breakpoint at a source location.

    The location is given either as a file name and a line number or as an
    `SBFileSpec`, a line number and optionally a column, an address offset and
    filters for the modules and compile units to search::

        breakpoint = target.BreakpointCreateByLocation('main.c', 42)

    The breakpoint is created even if no code matches the location; check
    `SBBreakpoint.GetNumLocations` to find out whether it resolved to
    anything."
) lldb::SBTarget::BreakpointCreateByLocation;

%feature("docstring", "
    Creates a breakpoint on all functions with the given name.

    ``name_type_mask`` is a logical OR of ``lldb.eFunctionNameType*`` values
    that selects how the name is matched (as a full name, a base name, a C++
    method or an Objective-C selector). ``module_name`` restricts the
    breakpoint to one module::

        breakpoint = target.BreakpointCreateByName('main', target.GetExecutable().GetFilename())

    See `SBTarget.BreakpointCreateByNames` to use several names at once and
    `SBTarget.BreakpointCreateByRegex` for regular expressions."
) lldb::SBTarget::BreakpointCreateByName;

%feature("docstring", "
    Creates a breakpoint on all functions matching any of the given names.

    Behaves like `SBTarget.BreakpointCreateByName` but takes a list of names,
    which is more efficient than creating one breakpoint per name::

        breakpoint = target.BreakpointCreateByNames(['malloc', 'free'], 2,
                                                    lldb.eFunctionNameTypeAuto,
                                                    lldb.SBFileSpecList(),
                                                    lldb.SBFileSpecList())
"
) lldb::SBTarget::BreakpointCreateByNames;

%feature("docstring", "
    Creates a breakpoint on all functions whose name matches a regular
    expression::

        # Break on every function whose name starts with 'test_'.
        breakpoint = target.BreakpointCreateByRegex('^test_')

    The regular expression syntax is the POSIX extended one. Optional
    parameters restrict the search to specific modules and compile units."
) lldb::SBTarget::BreakpointCreateByRegex;

%feature("docstring", "
    Creates a breakpoint on all source lines matching a regular expression.

    This is what the ``break set --source-pattern`` command does: instead of
    matching function names, the pattern is matched against the source code
    itself, which is handy to break on lines with a specific comment::

        breakpoint = target.BreakpointCreateBySourceRegex('// break here',
                                                          lldb.SBFileSpec('main.c'))
"
) lldb::SBTarget::BreakpointCreateBySourceRegex;

%feature("docstring", "
    Creates a breakpoint that stops when an exception is thrown or caught.

    ``language`` is one of the ``lldb.eLanguageType*`` enumerators and selects
    the exception mechanism to use (for example C++ or Objective-C
    exceptions)::

        breakpoint = target.BreakpointCreateForException(lldb.eLanguageTypeC_plus_plus,
                                                        False, True)
"
) lldb::SBTarget::BreakpointCreateForException;

%feature("docstring", "
    Creates a breakpoint at the given load address.

    See `SBTarget.BreakpointCreateBySBAddress` to use an `SBAddress`, which
    keeps working when the module is loaded at a different address."
) lldb::SBTarget::BreakpointCreateByAddress;

%feature("docstring", "
    Creates a breakpoint at the given `SBAddress`.

    Unlike `SBTarget.BreakpointCreateByAddress` this uses a section-relative
    address, so the breakpoint stays at the right place if the module is loaded
    at a different address."
) lldb::SBTarget::BreakpointCreateBySBAddress;

%feature("docstring", "
    Create a breakpoint using a scripted resolver.

    :param class_name:
       This is the name of the class that implements a scripted resolver.
       The class should have the following signature: ::

           class Resolver:
               def __init__(self, bkpt, extra_args):
                   # bkpt - the breakpoint for which this is the resolver.  When
                   # the resolver finds an interesting address, call AddLocation
                   # on this breakpoint to add it.
                   #
                   # extra_args - an SBStructuredData that can be used to
                   # parametrize this instance.  Same as the extra_args passed
                   # to BreakpointCreateFromScript.

               def __get_depth__ (self):
                   # This is optional, but if defined, you should return the
                   # depth at which you want the callback to be called.  The
                   # available options are:
                   #    lldb.eSearchDepthModule
                   #    lldb.eSearchDepthCompUnit
                   # The default if you don't implement this method is
                   # eSearchDepthModule.

               def __callback__(self, sym_ctx):
                   # sym_ctx - an SBSymbolContext that is the cursor in the
                   # search through the program to resolve breakpoints.
                   # The sym_ctx will be filled out to the depth requested in
                   # __get_depth__.
                   # Look in this sym_ctx for new breakpoint locations,
                   # and if found use bkpt.AddLocation to add them.
                   # Note, you will only get called for modules/compile_units that
                   # pass the SearchFilter provided by the module_list & file_list
                   # passed into BreakpointCreateFromScript.

               def get_short_help(self):
                   # Optional, but if implemented return a short string that will
                   # be printed at the beginning of the break list output for the
                   # breakpoint.

    :param extra_args:
       This is an SBStructuredData object that will get passed to the
       constructor of the class in class_name.  You can use this to
       reuse the same class, parametrizing it with entries from this
       dictionary.

    :param module_list:
       If this is non-empty, this will be used as the module filter in the
       SearchFilter created for this breakpoint.

    :param file_list:
       If this is non-empty, this will be used as the comp unit filter in the
       SearchFilter created for this breakpoint.

    :return:
        An SBBreakpoint that will set locations based on the logic in the
        resolver's search callback.
    :rtype: SBBreakpoint"
) lldb::SBTarget::BreakpointCreateFromScript;

%feature("docstring", "
    Read breakpoints from source_file and return the newly created
    breakpoints in bkpt_list.

    :param source_file: The file from which to read the breakpoints.
    :param matching_names: Only read in breakpoints whose names match one of
        the names in this list.
    :param bkpt_list: An `SBBreakpointList` that is filled in with the newly
        created breakpoints.
    :return: An SBError detailing any errors in reading in the breakpoints.
    :rtype: SBError

    Breakpoints are written to such a file with
    `SBTarget.BreakpointsWriteToFile`."
) lldb::SBTarget::BreakpointsCreateFromFile;

%feature("docstring", "
    Write breakpoints to dest_file.

    :param dest_file: The file to which to write the breakpoints.
    :param bkpt_list: Only write breakpoints from this `SBBreakpointList`. If
        this is omitted, all breakpoints of the target are written.
    :param append: If true, append the breakpoints in bkpt_list to the others
        serialized in dest_file.  If dest_file doesn't exist, then a new
        file will be created and the breakpoints in bkpt_list written to it.
    :return: An SBError detailing any errors in writing in the breakpoints.
    :rtype: SBError

    The resulting file can be read back with
    `SBTarget.BreakpointsCreateFromFile`."
) lldb::SBTarget::BreakpointsWriteToFile;

%feature("docstring", "
    Returns the number of breakpoints of this target.

    See `SBTarget.GetBreakpointAtIndex`; in Python
    ``target.breakpoint_iter()`` iterates over all breakpoints."
) lldb::SBTarget::GetNumBreakpoints;

%feature("docstring", "
    Returns the breakpoint at the given index as an `SBBreakpoint`.

    Note that this index is not the breakpoint ID, use
    `SBTarget.FindBreakpointByID` to look up a breakpoint by ID."
) lldb::SBTarget::GetBreakpointAtIndex;

%feature("docstring", "
    Deletes the breakpoint with the given ID and returns whether it existed."
) lldb::SBTarget::BreakpointDelete;

%feature("docstring", "
    Returns the breakpoint with the given ID as an `SBBreakpoint`.

    The ID is the number the ``breakpoint list`` command shows and what
    `SBBreakpoint.GetID` returns. Returns an invalid breakpoint if this target
    has no such breakpoint."
) lldb::SBTarget::FindBreakpointByID;

%feature("docstring", "
    Finds all breakpoints with the given name.

    Fills in the given `SBBreakpointList` and returns whether anything was
    found. See `SBBreakpoint.AddName` and `SBBreakpointName` for what
    breakpoint names are."
) lldb::SBTarget::FindBreakpointsByName;

%feature("docstring", "
    Fills the given `SBStringList` with all breakpoint names of this target.

    See `SBBreakpointName`."
) lldb::SBTarget::GetBreakpointNames;

%feature("docstring", "
    Removes a breakpoint name from this target.

    The breakpoints that had this name are not deleted, they just lose the
    name. See `SBBreakpointName`."
) lldb::SBTarget::DeleteBreakpointName;

%feature("docstring", "
    Enables all breakpoints of this target, see `SBBreakpoint.SetEnabled`."
) lldb::SBTarget::EnableAllBreakpoints;

%feature("docstring", "
    Disables all breakpoints of this target, see `SBBreakpoint.SetEnabled`."
) lldb::SBTarget::DisableAllBreakpoints;

%feature("docstring", "
    Deletes all breakpoints of this target."
) lldb::SBTarget::DeleteAllBreakpoints;

%feature("docstring", "
    Returns the number of watchpoints of this target.

    See `SBTarget.GetWatchpointAtIndex`; in Python
    ``target.watchpoint_iter()`` iterates over all watchpoints."
) lldb::SBTarget::GetNumWatchpoints;

%feature("docstring", "
    Returns the watchpoint at the given index as an `SBWatchpoint`."
) lldb::SBTarget::GetWatchpointAtIndex;

%feature("docstring", "
    Deletes the watchpoint with the given ID and returns whether it existed."
) lldb::SBTarget::DeleteWatchpoint;

%feature("docstring", "
    Returns the watchpoint with the given ID as an `SBWatchpoint`."
) lldb::SBTarget::FindWatchpointByID;

%feature("docstring", "
    Deprecated, use `SBTarget.WatchpointCreateByAddress` instead."
) lldb::SBTarget::WatchAddress;

%feature("docstring", "
    Sets a watchpoint on the given range of memory.

    The process stops when the memory is read from (``read``) or written to
    (``write``, or ``modify`` for the overload that takes an
    `SBWatchpointOptions`). Watchpoints usually use hardware support, so both
    the size and the number of watchpoints are limited, see
    `SBProcess.GetNumSupportedHardwareWatchpoints`::

        error = lldb.SBError()
        watchpoint = target.WatchpointCreateByAddress(addr, 4, options, error)

    See `SBValue.Watch` to watch the memory of a variable."
) lldb::SBTarget::WatchpointCreateByAddress;

%feature("docstring", "
    Enables all watchpoints of this target."
) lldb::SBTarget::EnableAllWatchpoints;

%feature("docstring", "
    Disables all watchpoints of this target."
) lldb::SBTarget::DisableAllWatchpoints;

%feature("docstring", "
    Deletes all watchpoints of this target."
) lldb::SBTarget::DeleteAllWatchpoints;

%feature("docstring", "
    Returns the `SBBroadcaster` of this target.

    Add it to an `SBListener` to receive the target's events, see
    `SBTarget.EventIsTargetEvent`."
) lldb::SBTarget::GetBroadcaster;

%feature("docstring", "
    Returns the first type with the given name as an `SBType`.

    Searches the debug information of all modules of this target::

        task_type = target.FindFirstType('Task')

    Returns an invalid type if nothing was found. Note that a program can
    contain several types with the same name, see `SBTarget.FindTypes`."
) lldb::SBTarget::FindFirstType;

%feature("docstring", "
    Returns all types with the given name as an `SBTypeList`.

    See `SBTarget.FindFirstType` if only one match is needed."
) lldb::SBTarget::FindTypes;

%feature("docstring", "
    Returns an `SBType` for one of the language's builtin types.

    ``type`` is one of the ``lldb.eBasicType*`` enumerators::

        int_type = target.GetBasicType(lldb.eBasicTypeInt)
"
) lldb::SBTarget::GetBasicType;

%feature("docstring", "
    Look up a persistent type defined using the expression parser.

    :param typename_cstr: The base name of the persistent type you defined.
    :param language: A member of the ``lldb.eLanguageType*`` enumerators giving
        the language of the Expression parser you used to define
        the persistent type.
    :param error: If there are errors fetching the type, they will be
        returned here.
    :return: An SBType representing the persistent type you defined.
    :rtype: SBType"
) lldb::SBTarget::FindExpressionTypeForLanguage;

%feature("docstring", "
    Look up a persistent variable defined using the expression parser.

    :param varname_cstr: The name of the persistent variable you defined,
        for example ``$my_var``.
    :param language: A member of the ``lldb.eLanguageType*`` enumerators giving
        the language of the Expression parser you used to define
        the persistent variable.
    :return: An SBValue representing the persistent variable you defined.
    :rtype: SBValue"
) lldb::SBTarget::FindExpressionVariableForLanguage;

%feature("docstring", "
    Create an SBValue with the given name by treating the memory starting at addr as an entity of type.

    :param name: The name of the resultant SBValue.
    :param addr: The `SBAddress` of the start of the memory region to be used.
    :param type: The `SBType` to use to interpret the memory starting at addr.
    :return: An SBValue of the given type, may be invalid if there was an error
        reading the underlying memory.
    :rtype: SBValue"
) lldb::SBTarget::CreateValueFromAddress;

%feature("docstring", "
    Creates an `SBValue` of the given type from raw bytes.

    ``data`` is an `SBData` holding the bytes of the value. The resulting value
    has no address in the target::

        data = lldb.SBData.CreateDataFromUInt32Array(target.GetByteOrder(),
                                                     target.GetAddressByteSize(), [42])
        value = target.CreateValueFromData('answer', data,
                                           target.GetBasicType(lldb.eBasicTypeInt))
"
) lldb::SBTarget::CreateValueFromData;

%feature("docstring", "
    Creates an `SBValue` from the result of an expression.

    The expression is evaluated in the scope of this target, i.e. without a
    frame, so only globals and statics are visible. See
    `SBTarget.EvaluateExpression`."
) lldb::SBTarget::CreateValueFromExpression;

%feature("docstring", "
    Returns the `SBSourceManager` of this target.

    Use it to read the source files that belong to this target's debug
    information."
) lldb::SBTarget::GetSourceManager;

%feature("docstring", "
    Disassemble a specified number of instructions starting at an address.

    :param base_addr: the address to start disassembly from.
    :param count: the number of instructions to disassemble.
    :param flavor_string: may be 'intel' or 'att' on x86 targets to specify that style of disassembly.
    :rtype: SBInstructionList

    The instructions are read from the target's memory if a process is running
    and from the object files otherwise::

        for instruction in target.ReadInstructions(function.GetStartAddress(), 10):
            print(instruction)
    "
) lldb::SBTarget::ReadInstructions;

%feature("docstring", "
    Disassemble the bytes in a buffer and return them in an SBInstructionList.

    :param base_addr: used for symbolicating the offsets in the byte stream when disassembling.
    :param buf: bytes to be disassembled.
    :param size: (C++) size of the buffer.
    :rtype: SBInstructionList

    Unlike `SBTarget.ReadInstructions` this disassembles bytes that are handed
    in instead of reading them from the target.
    "
) lldb::SBTarget::GetInstructions;

%feature("docstring", "
    Disassemble the bytes in a buffer and return them in an SBInstructionList, with a supplied flavor.

    :param base_addr: used for symbolicating the offsets in the byte stream when disassembling.
    :param flavor:  may be 'intel' or 'att' on x86 targets to specify that style of disassembly.
    :param buf: bytes to be disassembled.
    :param size: (C++) size of the buffer.
    :rtype: SBInstructionList
    "
) lldb::SBTarget::GetInstructionsWithFlavor;

%feature("docstring", "
    Finds symbols in the symbol tables of this target's modules.

    Unlike `SBTarget.FindFunctions` this searches the symbol table, so it also
    finds symbols without debug information and non-function symbols.
    ``matchtype`` is one of the ``lldb.eMatchType*`` enumerators::

        for context in target.FindSymbols('malloc'):
            print(context.GetSymbol().GetStartAddress())

    :rtype: SBSymbolContextList"
) lldb::SBTarget::FindSymbols;

%feature("docstring", "
    Writes a description of this target into the given `SBStream`.

    ``description_level`` is one of the ``lldb.eDescriptionLevel*``
    enumerators."
) lldb::SBTarget::GetDescription;

%feature("docstring", "
    Evaluates an expression in the context of this target.

    Since a target has no stack frame, only globals, statics and functions are
    visible to the expression; use `SBFrame.EvaluateExpression` to evaluate an
    expression with access to local variables::

        value = target.EvaluateExpression('(int)getpid()')

    ``options`` is an `SBExpressionOptions` controlling how the expression is
    run. Note that evaluating an expression can run code in the target."
) lldb::SBTarget::EvaluateExpression;

%feature("docstring", "
    Returns the size in bytes of the red zone of this target's ABI.

    The red zone is the area below the stack pointer that a function may use
    without adjusting the stack pointer. LLDB avoids clobbering it when it
    allocates memory on the stack of the target."
) lldb::SBTarget::GetStackRedZoneSize;

%feature("docstring", "
    Returns true if the module has been loaded in this `SBTarget`.
    A module can be loaded either by the dynamic loader or by being manually
    added to the target (see `SBTarget.AddModule` and the ``target module add`` command).

    :rtype: bool
    "
) lldb::SBTarget::IsLoaded;

%feature("docstring", "
    Returns the `SBLaunchInfo` that will be used when launching a process.

    The returned object is a copy; modify it and pass it to
    `SBTarget.SetLaunchInfo` or `SBTarget.Launch` to take effect."
) lldb::SBTarget::GetLaunchInfo;

%feature("docstring", "
    Sets the `SBLaunchInfo` to use for future launches of this target.

    This is how the arguments, environment and other launch settings can be set
    up once and then reused, for instance by the ``run`` command."
) lldb::SBTarget::SetLaunchInfo;

%feature("docstring", "
    Returns the `SBTrace` object that manages the processor trace of this
    target.

    The returned trace object might not be valid, for instance because no trace
    was created yet, so it should be checked with ``SBTrace.IsValid``. See
    `SBTarget.CreateTrace`."
) lldb::SBTarget::GetTrace;

%feature("docstring", "
    Creates an `SBTrace` for this target using the default tracing technology
    of the process, such as Intel Processor Trace.

    Fills in ``error`` if a trace already exists or if the trace couldn't be
    created."
) lldb::SBTarget::CreateTrace;

%feature("docstring", "
    Returns the `SBMutex` that guards this target's API.

    Lock it around a group of API calls that should not be interleaved with
    another thread's calls, for example when reading several related values::

        with target.GetAPIMutex():
            # No other thread can use the API of this target here.
            pass
"
) lldb::SBTarget::GetAPIMutex;

%feature("docstring", "
    Registers a scripted frame provider for this target.

    A frame provider is a Python class that can add synthetic frames to a
    thread's backtrace. If a provider with the same name and arguments is
    already registered on this target it is overwritten.

    :param class_name: The name of the Python class that implements the frame
        provider.
    :param args_dict: An `SBStructuredData` dictionary of arguments to pass to
        the frame provider class.
    :param error: An `SBError` indicating success or failure.
    :return: A unique identifier for the registered provider that can be passed
        to `SBTarget.RemoveScriptedFrameProvider`, or ``0`` if the registration
        failed."
) lldb::SBTarget::RegisterScriptedFrameProvider;

%feature("docstring", "
    Removes a scripted frame provider from this target.

    ``provider_id`` is the identifier that
    `SBTarget.RegisterScriptedFrameProvider` returned. Returns an `SBError`
    indicating success or failure."
) lldb::SBTarget::RemoveScriptedFrameProvider;
