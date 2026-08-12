%feature("docstring",
"Represents one of the stack frames of a thread.

A frame is one entry of a thread's call stack: it has a program counter, a
function (or symbol), a set of local variables and registers. Frames are
obtained from an `SBThread`, where frame ``0`` is the innermost (most recently
called) frame::

    frame = thread.GetFrameAtIndex(0)
    print('%s at %s' % (frame.GetFunctionName(), frame.GetLineEntry()))

In Python an `SBThread` is iterable, so a simple backtrace can be printed
with::

    for frame in thread:
        print(frame)

A frame gives access to the debug information of the code it is executing
(`SBFrame.GetModule`, `SBFrame.GetCompileUnit`, `SBFrame.GetFunction`,
`SBFrame.GetLineEntry`, `SBFrame.GetBlock`), to its data
(`SBFrame.FindVariable`, `SBFrame.GetVariables`, `SBFrame.GetRegisters`) and it
is the scope in which expressions are evaluated
(`SBFrame.EvaluateExpression`).

Note that frames become invalid as soon as the process resumes: after the
process is continued a new `SBFrame` has to be fetched from the thread.

For example, printing the arguments of every frame in a backtrace::

    for frame in thread:
        args = frame.GetVariables(True, False, False, True)
        arg_string = ', '.join('%s=%s' % (a.GetName(), a.GetValue()) for a in args)
        print('%s(%s)' % (frame.GetDisplayFunctionName(), arg_string))

See also `SBThread` for how to get frames and `SBValue` for what to do with the
variables of a frame."
) lldb::SBFrame;

%feature("docstring", "
    Returns whether this frame refers to the same frame as another SBFrame.

    Two frames are equal if they belong to the same thread and describe the
    same stack frame of the same function call. Note that frames from before
    and after resuming the process never compare equal."
) lldb::SBFrame::IsEqual;

%feature("docstring", "
    Returns whether this object refers to a stack frame.

    A frame becomes invalid when the process it belongs to resumes or exits, so
    frames should not be cached across stops."
) lldb::SBFrame::IsValid;

%feature("docstring", "
    Returns the index of this frame in its thread's call stack.

    Frame ``0`` is the innermost frame, i.e. the one that is currently
    executing. See `SBThread.GetFrameAtIndex`."
) lldb::SBFrame::GetFrameID;

%feature("docstring", "
    Get the Canonical Frame Address for this stack frame.
    This is the DWARF standard's definition of a CFA, a stack address
    that remains constant throughout the lifetime of the function.
    Returns an lldb::addr_t stack address, or LLDB_INVALID_ADDRESS if
    the CFA cannot be determined."
) lldb::SBFrame::GetCFA;

%feature("docstring", "
    Returns the program counter of this frame as an integer.

    For frames other than frame ``0`` this is the return address, i.e. the
    address the frame will continue executing at once the frame it called
    returns. See `SBFrame.GetPCAddress` for a section-relative address that can
    be resolved to symbols and line entries."
) lldb::SBFrame::GetPC;

%feature("docstring", "
    Sets the program counter of this frame.

    Returns ``False`` if the program counter could not be changed. This only
    works for frame ``0`` and is a low level operation: it does not adjust the
    stack, so jumping into an unrelated function will likely crash the process.
    See `SBThread.JumpToLine` for a safer alternative."
) lldb::SBFrame::SetPC;

%feature("docstring", "
    Returns the stack pointer of this frame as an integer."
) lldb::SBFrame::GetSP;

%feature("docstring", "
    Returns the frame pointer of this frame as an integer."
) lldb::SBFrame::GetFP;

%feature("docstring", "
    Returns the program counter of this frame as an `SBAddress`.

    Unlike `SBFrame.GetPC` the result is a section-relative address that can be
    resolved to a module, symbol or line entry even after the process
    exited."
) lldb::SBFrame::GetPCAddress;

%feature("docstring", "
    Returns the symbol context of this frame's program counter.

    ``resolve_scope`` is a bit mask of ``lldb.eSymbolContextXXX`` values that
    selects which parts of the context should be looked up, e.g.
    ``lldb.eSymbolContextEverything``::

        sc = frame.GetSymbolContext(lldb.eSymbolContextEverything)
        print(sc.GetFunction().GetName(), sc.GetLineEntry().GetLine())

    Looking up fewer parts is cheaper. See `SBSymbolContext`."
) lldb::SBFrame::GetSymbolContext;

%feature("docstring", "
    Returns the `SBModule` that contains the code this frame is executing."
) lldb::SBFrame::GetModule;

%feature("docstring", "
    Returns the `SBCompileUnit` the code of this frame was compiled from.

    Returns an invalid compile unit if there is no debug information for this
    frame."
) lldb::SBFrame::GetCompileUnit;

%feature("docstring", "
    Returns the `SBFunction` this frame is executing.

    Returns an invalid function if the frame has no debug information; use
    `SBFrame.GetSymbol` in that case, or `SBFrame.GetFunctionName` to get a
    name regardless of where it comes from."
) lldb::SBFrame::GetFunction;

%feature("docstring", "
    Returns the `SBSymbol` of the function this frame is executing.

    Symbols come from the symbol table of the module, so unlike
    `SBFrame.GetFunction` this also works without debug information."
) lldb::SBFrame::GetSymbol;

%feature("docstring", "
    Gets the deepest block that contains the frame PC.

    See also `SBFrame.GetFrameBlock`."
) lldb::SBFrame::GetBlock;

    %feature("docstring", "
    Get the appropriate function name for this frame. Inlined functions in
    LLDB are represented by Blocks that have inlined function information, so
    just looking at the SBFunction or SBSymbol for a frame isn't enough.
    This function will return the appropriate function, symbol or inlined
    function name for the frame.

    This function returns:
    - the name of the inlined function (if there is one)
    - the name of the concrete function (if there is one)
    - the name of the symbol (if there is one)
    - NULL

    See also `SBFrame.IsInlined` and `SBFrame.GetDisplayFunctionName`."
) lldb::SBFrame::GetFunctionName;

%feature("docstring", "
    Returns a function name for this frame that is suitable to show to a user.

    This can differ from `SBFrame.GetFunctionName`, for example because a
    language plugin or a frame recognizer provides a nicer name for the
    frame."
) lldb::SBFrame::GetDisplayFunctionName;

%feature("docstring", "
    Returns the language of the frame's SBFunction, or if there
    is no SBFunction, guess the language from the mangled name.

    The result is one of the ``lldb.eLanguageType*`` enumerators."
) lldb::SBFrame::GuessLanguage;

%feature("docstring", "
    Return true if this frame represents an inlined function.

    Inlined frames don't have a real stack frame of their own, they are part of
    the frame of the function they were inlined into.

    See also `SBFrame.GetFunctionName`."
) lldb::SBFrame::IsInlined;

%feature("docstring", "
    Returns whether this frame was created by a language runtime or plugin.

    Synthetic frames don't correspond to a real call frame in the target, they
    are inserted by LLDB to make a stack easier to understand (for example for
    asynchronous code)."
) lldb::SBFrame::IsSynthetic;

%feature("docstring", "
    Returns whether a frame recognizer decided to hide this frame.

    Hidden frames are omitted from backtraces the ``thread backtrace`` command
    prints, but they are still part of the thread's frame list."
) lldb::SBFrame::IsHidden;

%feature("docstring", "
    Returns language-specific runtime information about this frame.

    Language plugins can use this to report additional details, such as
    language version information or feature flags, as an `SBStructuredData`.
    Returns invalid structured data for frames whose language does not provide
    any."
) lldb::SBFrame::GetLanguageSpecificData;

%feature("docstring", "
    Returns an SBValueList which is an array of one or more register
    sets that exist for this thread.
    Each SBValue in the SBValueList represents one register-set.
    The first register-set will be the general purpose registers --
    the registers printed by the ``register read`` command-line in lldb, with
    no additional arguments.
    The register-set SBValue will have a name, e.g.
    ``SBFrame.GetRegisters().GetValueAtIndex(0).GetName()``
    By convention, certain stubs choose to name their general-purpose
    register-set the 'General Purpose Registers', but that is not required.
    A register-set SBValue will have children, one child per register
    in the register-set.

    See `SBFrame.FindRegister` to look up a single register by name."
) lldb::SBFrame::GetRegisters;

%feature("docstring", "
    Returns the register with the given name as an `SBValue`.

    The name is the one the target's register context uses, e.g. ``pc``,
    ``rax`` or ``x0``. Returns an invalid value if there is no such register::

        pc = frame.FindRegister('pc').GetValueAsUnsigned()

    See `SBFrame.GetRegisters` to enumerate all registers."
) lldb::SBFrame::FindRegister;

%feature("docstring", "
    Return true if this frame is artificial (e.g a frame synthesized to
    capture a tail call). Local variables may not be available in an artificial
    frame."
) lldb::SBFrame::IsArtificial;

%feature("docstring", "
    Evaluates an expression in the scope of this frame and returns the result.

    The variables of this frame are visible to the expression, so it can be
    written just like source code at the frame's current location::

        value = frame.EvaluateExpression('argc + 1')
        if value.GetError().Success():
            print(value.GetValueAsSigned())

    ``options`` is an `SBExpressionOptions` that controls, among other things,
    the language of the expression, the timeout and whether the expression may
    call functions in the target. The version that doesn't supply a
    'use_dynamic' value will use the target's default.

    Note that evaluating an expression can run code in the target, which can
    hit breakpoints and change the state of the process. Use
    `SBFrame.GetValueForVariablePath` or `SBFrame.FindVariable` instead if all
    that is needed is the value of a variable.

    See also `SBTarget.EvaluateExpression` and `SBValue.EvaluateExpression`."
) lldb::SBFrame::EvaluateExpression;

%feature("docstring", "
    Gets the lexical block that defines the stack frame. Another way to think
    of this is it will return the block that contains all of the variables
    for a stack frame. Inlined functions are represented as SBBlock objects
    that have inlined function information: the name of the inlined function,
    where it was called from. The block that is returned will be the first
    block at or above the block for the PC (`SBFrame.GetBlock`) that defines
    the scope of the frame. When a function contains no inlined functions,
    this will be the top most lexical block that defines the function.
    When a function has inlined functions and the PC is currently
    in one of those inlined functions, this method will return the inlined
    block that defines this frame. If the PC isn't currently in an inlined
    function, the lexical block that defines the function is returned."
) lldb::SBFrame::GetFrameBlock;

%feature("docstring", "
    Returns the source file and line this frame is stopped at as an
    `SBLineEntry`.

    Returns an invalid line entry if there is no line table information for the
    frame's program counter."
) lldb::SBFrame::GetLineEntry;

%feature("docstring", "
    Returns the `SBThread` this frame belongs to."
) lldb::SBFrame::GetThread;

%feature("docstring", "
    Returns the disassembly of the function of this frame as a string.

    This disassembles the whole function, with the current instruction marked.
    See `SBTarget.ReadInstructions` for programmatic access to individual
    instructions."
) lldb::SBFrame::Disassemble;

%feature("docstring", "
    Resets this object to an invalid frame."
) lldb::SBFrame::Clear;

%feature("docstring", "
    Returns the variables of this frame as an `SBValueList`.

    The boolean parameters select which kinds of variables are returned:
    ``arguments`` for the function's parameters, ``locals`` for local
    variables, ``statics`` for static and global variables visible in this
    frame and ``in_scope_only`` to skip variables whose lexical scope does not
    contain the current program counter::

        for var in frame.GetVariables(True, True, False, True):
            print('%s = %s' % (var.GetName(), var.GetValue()))

    The overload that takes an `SBVariablesOptions` allows more control, for
    instance over whether values are shown with their dynamic type.

    The version that doesn't supply a 'use_dynamic' value will use the
    target's default."
) lldb::SBFrame::GetVariables;

%feature("docstring", "
    Returns the variable with the given name as an `SBValue`.

    Only looks for variables that are in scope in this frame; registers and
    persistent expression variables are not found. Returns an invalid value if
    there is no such variable::

        argc = frame.FindVariable('argc')

    See `SBFrame.FindValue` to also search registers and persistent variables,
    and `SBFrame.GetValueForVariablePath` for member access.

    The version that doesn't supply a 'use_dynamic' value will use the
    target's default."
) lldb::SBFrame::FindVariable;

%feature("docstring", "
    Get a lldb.SBValue for a variable path.

    Variable paths can include access to pointer or instance members: ::

        rect_ptr->origin.y
        pt.x

    Pointer dereferences: ::

        *this->foo_ptr
        **argv

    Address of: ::

        &pt
        &my_array[3].x

    Array accesses and treating pointers as arrays: ::

        int_array[1]
        pt_ptr[22].x

    Unlike `SBFrame.EvaluateExpression` which returns :py:class:`SBValue` objects
    with constant copies of the values at the time of evaluation,
    the result of this function is a value that will continue to
    track the current value of the value as execution progresses
    in the current frame. It also doesn't compile or run any code, which makes
    it considerably faster than evaluating an expression."
) lldb::SBFrame::GetValueForVariablePath;

%feature("docstring", "
    Find variables, register sets, registers, or persistent variables using
    the frame as the scope.

    ``value_type`` is one of the ``lldb.eValueType*`` enumerators and selects
    what is searched for, e.g. ``lldb.eValueTypeVariableLocal`` for a local
    variable, ``lldb.eValueTypeRegister`` for a register or
    ``lldb.eValueTypeConstResult`` for a persistent expression result such as
    ``$0``::

        rax = frame.FindValue('rax', lldb.eValueTypeRegister)

    Note that this does not look up instance variables through the ``this`` or
    ``self`` pointer, use `SBFrame.GetValueForVariablePath` for that.

    The version that doesn't supply a ``use_dynamic`` value will use the
    target's default."
) lldb::SBFrame::FindValue;

%feature("docstring", "
    Writes a description of this frame into the given `SBStream`.

    The description is similar to one line of the ``thread backtrace``
    output. See `SBFrame.GetDescriptionWithFormat` for a customizable
    version."
) lldb::SBFrame::GetDescription;

%feature("docstring", "
    Writes a description of this frame into the given `SBStream`, using a
    custom format.

    ``format`` is an `SBFormat` created from a format string as described in
    https://lldb.llvm.org/use/formatting.html. Returns an `SBError` describing
    any problem with the format string or the frame::

        error = lldb.SBError()
        format = lldb.SBFormat('${function.name} at ${line.file.basename}:${line.number}', error)
        stream = lldb.SBStream()
        frame.GetDescriptionWithFormat(format, stream)
        print(stream.GetData())
"
) lldb::SBFrame::GetDescriptionWithFormat;
