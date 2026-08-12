%feature("docstring",
"Represents a general way to provide a type name to LLDB APIs.

A name specifier says which types a data formatter applies to. It is either an
exact type name, a regular expression that type names are matched against, or an
`SBType`::

    # Every type whose name is exactly 'Task'.
    lldb.SBTypeNameSpecifier('Task')

    # Every type whose name starts with 'Task'.
    lldb.SBTypeNameSpecifier('^Task', True)

Name specifiers are what the ``Add*`` and ``Delete*`` functions of
`SBTypeCategory` take."
) lldb::SBTypeNameSpecifier;

%feature("docstring",
"Returns whether this object holds a type name specifier."
) lldb::SBTypeNameSpecifier::IsValid;

%feature("docstring",
"Returns the type name or the regular expression of this specifier."
) lldb::SBTypeNameSpecifier::GetName;

%feature("docstring",
"Returns the `SBType` of this specifier.

Only valid for specifiers that were created from a type rather than from a
name."
) lldb::SBTypeNameSpecifier::GetType;

%feature("docstring",
"Returns how the name of this specifier is matched.

The result is one of the ``lldb.eFormatterMatch*`` enumerators, which
distinguishes an exact name from a regular expression and from a Python callback
that decides whether a type matches."
) lldb::SBTypeNameSpecifier::GetMatchType;

%feature("docstring",
"Returns whether the name of this specifier is a regular expression."
) lldb::SBTypeNameSpecifier::IsRegex;

%feature("docstring",
"Writes a description of this specifier into the given `SBStream`."
) lldb::SBTypeNameSpecifier::GetDescription;

%feature("docstring",
"Returns whether this specifier is the same as another one."
) lldb::SBTypeNameSpecifier::IsEqualTo;
