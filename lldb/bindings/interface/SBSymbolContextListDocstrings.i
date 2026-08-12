%feature("docstring",
"Represents a list of `SBSymbolContext` objects.

Symbol context lists are what the lookup functions that can produce several
results return, for example `SBTarget.FindFunctions`,
`SBTarget.FindSymbols`, `SBTarget.FindCompileUnits` and
`SBModule.FindFunctions`.

In Python an SBSymbolContextList supports ``len()``, indexing and iteration::

    for context in target.FindFunctions('c', lldb.eFunctionNameTypeAuto):
        print('%s in %s' % (context.GetSymbol().GetName(),
                            context.GetModule().GetFileSpec().GetFilename()))

See also :py:class:`SBSymbolContext`."
) lldb::SBSymbolContextList;

%feature("docstring",
"Returns whether this list was initialized.

A valid list can still be empty, see `SBSymbolContextList.GetSize`."
) lldb::SBSymbolContextList::IsValid;

%feature("docstring",
"Returns the number of symbol contexts in this list.

In Python this is also what ``len()`` returns."
) lldb::SBSymbolContextList::GetSize;

%feature("docstring",
"Returns the symbol context at the given index as an `SBSymbolContext`."
) lldb::SBSymbolContextList::GetContextAtIndex;

%feature("docstring",
"Writes a description of all symbol contexts in this list into the given
`SBStream`."
) lldb::SBSymbolContextList::GetDescription;

%feature("docstring",
"Appends a single `SBSymbolContext` or all contexts of another
`SBSymbolContextList` to this list."
) lldb::SBSymbolContextList::Append;

%feature("docstring",
"Removes all symbol contexts from this list."
) lldb::SBSymbolContextList::Clear;
