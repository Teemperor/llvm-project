%feature("docstring",
"Represents a format that can be associated to one or more types.

A type format decides how the value of a type is printed: as hexadecimal, as a
character, as a boolean and so on. It is the API equivalent of the ``type format
add`` command and it is registered with `SBTypeCategory.AddTypeFormat`::

    # Print every value of type 'MyHandle' in hexadecimal.
    fmt = lldb.SBTypeFormat(lldb.eFormatHex)
    debugger.GetDefaultCategory().AddTypeFormat(lldb.SBTypeNameSpecifier('MyHandle'), fmt)

A format can also be created from the name of another type, in which case values
are displayed as if they had that type. See `SBTypeSummary` for a formatter that
produces a whole string for a value, and :doc:`/use/variable` for the data
formatter system as a whole."
) lldb::SBTypeFormat;

%feature("docstring",
"Returns whether this object refers to a format."
) lldb::SBTypeFormat::IsValid;

%feature("docstring",
"Returns the format as one of the ``lldb.eFormat*`` enumerators."
) lldb::SBTypeFormat::GetFormat;

%feature("docstring",
"Returns the name of the type whose format is used.

Only set for formats that were created from a type name instead of an
``lldb.eFormat*`` value."
) lldb::SBTypeFormat::GetTypeName;

%feature("docstring",
"Returns the options of this format as a bit mask of the
``lldb.eTypeOption*`` values."
) lldb::SBTypeFormat::GetOptions;

%feature("docstring",
"Sets the format, see `SBTypeFormat.GetFormat`."
) lldb::SBTypeFormat::SetFormat;

%feature("docstring",
"Sets the name of the type whose format should be used, see
`SBTypeFormat.GetTypeName`."
) lldb::SBTypeFormat::SetTypeName;

%feature("docstring",
"Sets the options of this format.

``options`` is a bit mask of the ``lldb.eTypeOption*`` values, which control for
example whether the format also applies to the children of a value
(``lldb.eTypeOptionCascade``)."
) lldb::SBTypeFormat::SetOptions;

%feature("docstring",
"Writes a description of this format into the given `SBStream`."
) lldb::SBTypeFormat::GetDescription;

%feature("docstring",
"Returns whether this format is the same as another one."
) lldb::SBTypeFormat::IsEqualTo;
