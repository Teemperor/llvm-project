%feature("docstring",
"Represents an entry of the symbol table of a module.

Symbols come from the symbol table of an object file, so unlike `SBFunction`
they are available even without debug information. They describe functions,
global variables, Objective-C classes and various other kinds of entities; see
`SBSymbol.GetType`.

:py:class:`SBModule` contains SBSymbol(s), which can be reached with
`SBModule.GetSymbolAtIndex`, `SBModule.FindSymbol` or `SBTarget.FindSymbols`.
SBSymbol can also be retrieved from :py:class:`SBFrame` with
`SBFrame.GetSymbol` and from an address with `SBAddress.GetSymbol`::

    symbol = frame.GetSymbol()
    print('%s: %s - %s' % (symbol.GetName(), symbol.GetStartAddress(),
                           symbol.GetEndAddress()))
"
) lldb::SBSymbol;

%feature("docstring",
"Returns whether this object refers to a symbol."
) lldb::SBSymbol::IsValid;

%feature("docstring",
"Returns the name of this symbol.

For mangled symbols this is the demangled name. See
`SBSymbol.GetMangledName` for the name as it appears in the symbol table and
`SBSymbol.GetDisplayName` for the name to show to users."
) lldb::SBSymbol::GetName;

%feature("docstring",
"Returns the name of this symbol in a form that is meant to be shown to users.

This can be shorter than `SBSymbol.GetName`, for example because a language
plugin knows how to abbreviate the name."
) lldb::SBSymbol::GetDisplayName;

%feature("docstring",
"Returns the mangled name of this symbol, if it has one.

This is the name as it appears in the object file, e.g.
``_ZN3Foo3barEv``. Returns ``None`` for symbols that are not mangled."
) lldb::SBSymbol::GetMangledName;

%feature("docstring",
"Returns the base name of this symbol.

For a C++ method this is the method name without its class and its arguments,
e.g. ``bar`` for ``Foo::bar(int)``."
) lldb::SBSymbol::GetBaseName;

%feature("docstring",
"Returns the instructions of this symbol as an `SBInstructionList`.

``target`` provides the memory and the disassembler to use and ``flavor_string``
optionally selects the disassembly flavor (``intel`` or ``att`` on x86)::

    for instruction in symbol.GetInstructions(target):
        print(instruction)
"
) lldb::SBSymbol::GetInstructions;

%feature("docstring",
"Returns the start address of this symbol as an `SBAddress`.

Returns an invalid address if the symbol's value is not an address, see
`SBSymbol.GetValue`."
) lldb::SBSymbol::GetStartAddress;

%feature("docstring",
"Returns the address after the last byte of this symbol as an `SBAddress`.

Returns an invalid address if the symbol's value is not an address."
) lldb::SBSymbol::GetEndAddress;

%feature("docstring",
"Returns the raw value of this symbol from the symbol table.

The value can be a file address or an integer whose meaning depends on the
symbol's type. `SBSymbol.GetStartAddress` only returns something for symbols
whose value is an address, so this is the way to read the value of the
others."
) lldb::SBSymbol::GetValue;

%feature("docstring",
"Returns the size of this symbol as recorded in the symbol table.

For function symbols this is the size of the function in bytes."
) lldb::SBSymbol::GetSize;

%feature("docstring",
"Returns the size in bytes of this function's prologue.

The prologue is the code at the start of a function that sets up its stack
frame. Breakpoints are usually placed after it so that the arguments of the
function are available."
) lldb::SBSymbol::GetPrologueByteSize;

%feature("docstring",
"Returns what kind of symbol this is as one of the ``lldb.eSymbolType*``
enumerators.

Common values are ``lldb.eSymbolTypeCode`` for functions and
``lldb.eSymbolTypeData`` for variables. See `SBSymbol.GetTypeAsString` for a
human readable version."
) lldb::SBSymbol::GetType;

%feature("docstring",
"Returns the ID of this symbol, usually its index in the symbol table.

Returns ``lldb.LLDB_INVALID_SYMBOL_ID`` for an invalid symbol."
) lldb::SBSymbol::GetID;

%feature("docstring",
"Writes a description of this symbol into the given `SBStream`."
) lldb::SBSymbol::GetDescription;

%feature("docstring",
"Returns whether this symbol is visible outside of its object file.

External symbols are the ones the linker can see, i.e. non-``static``
functions and variables in C."
) lldb::SBSymbol::IsExternal;

%feature("docstring",
"Returns whether this symbol was synthesized by LLDB.

Synthetic symbols are not in the object file's symbol table; LLDB creates them
for example for functions it found in the runtime or in unwind information."
) lldb::SBSymbol::IsSynthetic;

%feature("docstring",
"Returns whether this symbol is a debug symbol.

Debug symbols exist for the debugger's benefit only; they are not needed to
link or run the program."
) lldb::SBSymbol::IsDebug;

%feature("docstring",
"Returns the name of a symbol type as a string.

``symbol_type`` is one of the ``lldb.eSymbolType*`` enumerators::

    print(lldb.SBSymbol.GetTypeAsString(symbol.GetType()))
"
) lldb::SBSymbol::GetTypeAsString;

%feature("docstring",
"Returns the ``lldb.eSymbolType*`` enumerator for a symbol type name.

This is the inverse of `SBSymbol.GetTypeAsString`."
) lldb::SBSymbol::GetTypeFromString;
