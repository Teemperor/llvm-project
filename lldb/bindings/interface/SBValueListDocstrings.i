%feature("docstring",
"Represents a list of `SBValue` objects.

Value lists are returned by the API functions that can produce more than one
value, for example `SBFrame.GetVariables`, `SBFrame.GetRegisters` and
`SBTarget.FindGlobalVariables`.

In Python an SBValueList behaves like a read-only sequence: it supports
``len()``, iteration and indexing. Indexing with a string returns the list of
all values with that name, and indexing with a compiled regular expression
returns all values whose name matches::

    variables = frame.GetVariables(True, True, True, True)
    print('%d variables in scope' % len(variables))
    for value in variables:
        print('%s %s = %s' % (value.GetTypeName(), value.GetName(), value.GetValue()))

    # All variables called 'argc'.
    argc_values = variables['argc']

    # All variables whose name starts with 'm_'.
    import re
    members = variables[re.compile('^m_')]

Registers are grouped into sets, so the values of `SBFrame.GetRegisters` are
themselves aggregates whose children are the actual registers::

    for register_set in frame.GetRegisters():
        print(register_set.GetName())
        for register in register_set:
            print('  %s = %s' % (register.GetName(), register.GetValue()))
"
) lldb::SBValueList;

%feature("docstring",
"Returns whether this list was initialized.

Note that a valid list can still be empty; use `SBValueList.GetSize` to check
for that."
) lldb::SBValueList::IsValid;

%feature("docstring",
"Removes all values from this list."
) lldb::SBValueList::Clear;

%feature("docstring",
"Appends a single `SBValue` or all values of another SBValueList to this list."
) lldb::SBValueList::Append;

%feature("docstring",
"Returns the number of values in this list.

In Python this is also what ``len()`` returns."
) lldb::SBValueList::GetSize;

%feature("docstring",
"Returns the value at the given index.

Returns an invalid `SBValue` if the index is out of bounds. In Python the
list can also be indexed directly, including with negative indices."
) lldb::SBValueList::GetValueAtIndex;

%feature("docstring",
"Returns the first value in this list with the given name.

Returns an invalid `SBValue` if no value has that name. In Python, indexing the
list with a string returns *all* values with that name instead."
) lldb::SBValueList::GetFirstValueByName;

%feature("docstring",
"Returns the value in this list with the given unique identifier.

The identifier is the one returned by `SBValue.GetID`. Returns an invalid
`SBValue` if this list has no such value."
) lldb::SBValueList::FindValueObjectByUID;

%feature("docstring",
"Returns an `SBError` describing why this list could not be produced.

Functions that return an SBValueList use this to report failures, so an empty
list with an error is different from a list that is legitimately empty."
) lldb::SBValueList::GetError;
