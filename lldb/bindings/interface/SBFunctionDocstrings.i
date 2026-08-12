%feature("docstring",
"Represents a function of the debugged program as described by debug
information.

A function is only available if the module it belongs to has debug information;
`SBSymbol` is the equivalent that is built from the symbol table and works
without it. Functions are obtained from `SBFrame.GetFunction`,
`SBAddress.GetFunction`, `SBSymbolContext.GetFunction` or by searching with
`SBTarget.FindFunctions`.

Besides its name and address range, a function gives access to its type
(`SBFunction.GetType`), its lexical blocks and variables
(`SBFunction.GetBlock`) and its instructions
(`SBFunction.GetInstructions`).

For example, printing where a frame's function is defined and what it looks
like::

    function = frame.GetFunction()
    if function:
        print('%s at %s' % (function.GetName(), function.GetStartAddress()))
        for instruction in function.GetInstructions(target):
            print(instruction)
    else:
        # No debug info, fall back to the symbol table.
        print(frame.GetSymbol().GetName())

See also :py:class:`SBSymbol`, :py:class:`SBBlock` and
:py:class:`SBLineEntry`."
) lldb::SBFunction;

%feature("docstring", "
    Returns whether this object refers to a function."
) lldb::SBFunction::IsValid;

%feature("docstring", "
    Returns the name of this function.

    For mangled languages this is the demangled name including the arguments,
    e.g. ``Foo::bar(int)``. See `SBFunction.GetBaseName` and
    `SBFunction.GetMangledName`."
) lldb::SBFunction::GetName;

%feature("docstring", "
    Returns the name of this function in a form that is meant to be shown to
    users.

    This can be shorter than `SBFunction.GetName`, for example because a
    language plugin knows how to abbreviate the name."
) lldb::SBFunction::GetDisplayName;

%feature("docstring", "
    Returns the mangled name of this function, if it has one.

    Returns ``None`` for languages that don't mangle names."
) lldb::SBFunction::GetMangledName;

%feature("docstring", "
    Returns the base name of this function.

    This is the name without its class and without its arguments, e.g. ``bar``
    for ``Foo::bar(int)``."
) lldb::SBFunction::GetBaseName;

%feature("docstring", "
    Returns the instructions of this function as an `SBInstructionList`.

    ``target`` provides the memory and the disassembler to use, and
    ``flavor_string`` optionally selects the disassembly flavor (``intel`` or
    ``att`` on x86)::

        for instruction in function.GetInstructions(target):
            print('%s %s' % (instruction.GetMnemonic(target),
                             instruction.GetOperands(target)))
"
) lldb::SBFunction::GetInstructions;

%feature("docstring", "
    Returns the first address of this function as an `SBAddress`."
) lldb::SBFunction::GetStartAddress;

%feature("docstring", "
    Returns the address after the last byte of this function.

    Deprecated: a function can consist of several discontinuous address ranges,
    in which case this is not meaningful. Use `SBFunction.GetRanges`
    instead."
) lldb::SBFunction::GetEndAddress;

%feature("docstring", "
    Returns the address ranges of this function as an `SBAddressRangeList`.

    Most functions have exactly one range, but a compiler may split a function
    into several discontinuous ranges, for example to move cold code out of the
    way."
) lldb::SBFunction::GetRanges;

%feature("docstring", "
    Returns the name of the argument with the given index.

    Returns ``None`` if there is no argument with that index. For C++ methods
    index ``0`` is the implicit ``this`` argument."
) lldb::SBFunction::GetArgumentName;

%feature("docstring", "
    Returns the size in bytes of this function's prologue.

    The prologue is the code at the start of a function that sets up its stack
    frame. Breakpoints are usually placed after it so that the arguments of the
    function are available."
) lldb::SBFunction::GetPrologueByteSize;

%feature("docstring", "
    Returns the type of this function as an `SBType`.

    Use `SBType.GetFunctionReturnType` and
    `SBType.GetFunctionArgumentTypes` on the result to inspect the
    signature."
) lldb::SBFunction::GetType;

%feature("docstring", "
    Returns the top level lexical block of this function as an `SBBlock`.

    The block and its children describe the scopes of the function and the
    variables that are visible in them, see `SBBlock.GetVariables`."
) lldb::SBFunction::GetBlock;

%feature("docstring", "
    Returns the language this function was written in as one of the
    ``lldb.eLanguageType*`` enumerators."
) lldb::SBFunction::GetLanguage;

%feature("docstring", "
    Returns true if the function was compiled with optimization.
    Optimization, in this case, is meant to indicate that the debugger
    experience may be confusing for the user -- variables optimized away,
    stepping jumping between source lines -- and the driver may want to
    provide some guidance to the user about this.
    Returns false if unoptimized, or unknown."
) lldb::SBFunction::GetIsOptimized;

%feature("docstring", "
    Writes a description of this function into the given `SBStream`."
) lldb::SBFunction::GetDescription;
