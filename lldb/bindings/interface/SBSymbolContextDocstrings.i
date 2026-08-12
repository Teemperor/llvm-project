%feature("docstring",
"A context object that provides access to core debugger entities.

Many debugger functions require a context when doing lookups. This class
provides a common structure that can be used as the result of a query that
can contain a single result.

A symbol context bundles everything LLDB knows about one address: the module it
belongs to, the compile unit and function it is in, the innermost lexical block,
the line entry and the symbol. Any of those can be missing, for example the
compile unit and function are invalid for code without debug information.

Symbol contexts are produced by `SBFrame.GetSymbolContext`,
`SBAddress.GetSymbolContext` and
`SBTarget.ResolveSymbolContextForAddress`, all of which take a bit mask of
``lldb.eSymbolContext*`` values that selects which parts to look up (looking up
fewer parts is cheaper).

For example, ::

        exe = os.path.join(os.getcwd(), 'a.out')

        # Create a target for the debugger.
        target = self.dbg.CreateTarget(exe)

        # Now create a breakpoint on main.c by name 'c'.
        breakpoint = target.BreakpointCreateByName('c', 'a.out')

        # Now launch the process, and do not stop at entry point.
        process = target.LaunchSimple(None, None, os.getcwd())

        # The inferior should stop on 'c'.
        from lldbutil import get_stopped_thread
        thread = get_stopped_thread(process, lldb.eStopReasonBreakpoint)
        frame0 = thread.GetFrameAtIndex(0)

        # Now get the SBSymbolContext from this frame.  We want everything. :-)
        context = frame0.GetSymbolContext(lldb.eSymbolContextEverything)

        # Get the module.
        module = context.GetModule()
        ...

        # And the compile unit associated with the frame.
        compileUnit = context.GetCompileUnit()
        ...

See also :py:class:`SBSymbolContextList`, which is what the lookup functions
that can return several results use."
) lldb::SBSymbolContext;

%feature("docstring",
"Returns whether this object holds a symbol context.

Note that a valid symbol context can still have invalid members, for example
when there is no debug information for the address it describes."
) lldb::SBSymbolContext::IsValid;

%feature("docstring",
"Returns the `SBModule` of this context."
) lldb::SBSymbolContext::GetModule;

%feature("docstring",
"Returns the `SBCompileUnit` of this context.

Invalid if the address this context describes has no debug information."
) lldb::SBSymbolContext::GetCompileUnit;

%feature("docstring",
"Returns the `SBFunction` of this context.

Invalid if the address this context describes has no debug information; use
`SBSymbolContext.GetSymbol` in that case."
) lldb::SBSymbolContext::GetFunction;

%feature("docstring",
"Returns the innermost `SBBlock` of this context.

For addresses inside an inlined function this is the block of the inlined
function, see `SBBlock.GetContainingInlinedBlock`."
) lldb::SBSymbolContext::GetBlock;

%feature("docstring",
"Returns the `SBLineEntry` of this context, i.e. the source file and line."
) lldb::SBSymbolContext::GetLineEntry;

%feature("docstring",
"Returns the `SBSymbol` of this context.

Unlike `SBSymbolContext.GetFunction` this also works without debug
information."
) lldb::SBSymbolContext::GetSymbol;

%feature("docstring",
"Sets the `SBModule` of this context."
) lldb::SBSymbolContext::SetModule;

%feature("docstring",
"Sets the `SBCompileUnit` of this context."
) lldb::SBSymbolContext::SetCompileUnit;

%feature("docstring",
"Sets the `SBFunction` of this context."
) lldb::SBSymbolContext::SetFunction;

%feature("docstring",
"Sets the `SBBlock` of this context."
) lldb::SBSymbolContext::SetBlock;

%feature("docstring",
"Sets the `SBLineEntry` of this context."
) lldb::SBSymbolContext::SetLineEntry;

%feature("docstring",
"Sets the `SBSymbol` of this context."
) lldb::SBSymbolContext::SetSymbol;

%feature("docstring",
"Returns the symbol context of the scope this inlined function was inlined
into.

``curr_frame_pc`` is the program counter this context was looked up for and
``parent_frame_addr`` receives the address of the call site in the enclosing
scope. Only meaningful for contexts inside inlined functions; walking these
contexts is how LLDB presents the chain of inlined calls of a frame."
) lldb::SBSymbolContext::GetParentOfInlinedScope;

%feature("docstring",
"Writes a description of this symbol context into the given `SBStream`."
) lldb::SBSymbolContext::GetDescription;
