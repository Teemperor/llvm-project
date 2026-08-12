%feature("docstring",
"Specifies an association with a line and column for a variable.

A declaration describes where in the source code a variable was declared. It is
obtained from `SBValue.GetDeclaration` and is only available for values that
come from debug information::

    declaration = frame.FindVariable('argc').GetDeclaration()
    print('declared at %s:%d' % (declaration.GetFileSpec().GetFilename(),
                                 declaration.GetLine()))

See also :py:class:`SBLineEntry`, which describes the source location of code
instead of a declaration."
) lldb::SBDeclaration;

%feature("docstring",
"Returns whether this object refers to a declaration."
) lldb::SBDeclaration::IsValid;

%feature("docstring",
"Returns the file this variable was declared in as an `SBFileSpec`."
) lldb::SBDeclaration::GetFileSpec;

%feature("docstring",
"Returns the line this variable was declared on.

Returns ``0`` if the debug information contains no line for the declaration."
) lldb::SBDeclaration::GetLine;

%feature("docstring",
"Returns the column this variable was declared at.

Returns ``0`` if the debug information contains no column for the declaration."
) lldb::SBDeclaration::GetColumn;

%feature("docstring",
"Sets the file of this declaration.

Only changes this object, not the debug information."
) lldb::SBDeclaration::SetFileSpec;

%feature("docstring",
"Sets the line of this declaration, see
`SBDeclaration.SetFileSpec`."
) lldb::SBDeclaration::SetLine;

%feature("docstring",
"Sets the column of this declaration, see
`SBDeclaration.SetFileSpec`."
) lldb::SBDeclaration::SetColumn;

%feature("docstring",
"Writes a description of this declaration into the given `SBStream`."
) lldb::SBDeclaration::GetDescription;
