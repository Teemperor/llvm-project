%feature("docstring",
"Options that are passed to a summary provider.

A summary provider that is implemented in Python receives an
SBTypeSummaryOptions object as its second argument. It says in which language the
value should be described and whether the summary may be capped to a maximum
length::

    def my_summary(value, internal_dict, options):
        if options.GetCapping() == lldb.eTypeSummaryUncapped:
            return full_description(value)
        return short_description(value)

See `SBTypeSummary` for registering such a provider."
) lldb::SBTypeSummaryOptions;

%feature("docstring",
"Returns whether this object holds a set of options."
) lldb::SBTypeSummaryOptions::IsValid;

%feature("docstring",
"Returns the language the summary is generated for.

The result is one of the ``lldb.eLanguageType*`` enumerators."
) lldb::SBTypeSummaryOptions::GetLanguage;

%feature("docstring",
"Returns whether the summary may be shortened.

The result is either ``lldb.eTypeSummaryCapped``, which means LLDB will truncate
a long summary, or ``lldb.eTypeSummaryUncapped``, which means the full summary is
wanted (this is what ``frame variable`` uses when a value is printed on its
own)."
) lldb::SBTypeSummaryOptions::GetCapping;

%feature("docstring",
"Sets the language the summary is generated for."
) lldb::SBTypeSummaryOptions::SetLanguage;

%feature("docstring",
"Sets whether the summary may be shortened, see
`SBTypeSummaryOptions.GetCapping`."
) lldb::SBTypeSummaryOptions::SetCapping;

%feature("docstring",
"Represents a summary that can be associated to one or more types.

A summary is the one line description LLDB shows for a value instead of listing
all of its members, for example the contents of a ``std::string`` or the ``id``
of a task. It is the API equivalent of the ``type summary add`` command and it is
registered with `SBTypeCategory.AddTypeSummary`.

A summary can be a summary string, which is a format string as described in
:doc:`/use/variable`, or a Python function::

    # With a summary string.
    summary = lldb.SBTypeSummary.CreateWithSummaryString('id = ${var.id}')
    debugger.GetDefaultCategory().AddTypeSummary(lldb.SBTypeNameSpecifier('Task'),
                                                summary)

    # With a Python function that takes (value, internal_dict, options).
    summary = lldb.SBTypeSummary.CreateWithFunctionName('my_module.task_summary')

The summary of a value is available through `SBValue.GetSummary`. See
`SBTypeSynthetic` for changing the children of a value instead of its
description."
) lldb::SBTypeSummary;

%feature("docstring",
"Creates a summary from a summary format string.

The string can refer to the value with ``${var}`` and to its members with
``${var.member}``, see :doc:`/use/variable` for the full syntax. ``options`` is a
bit mask of the ``lldb.eTypeOption*`` values::

    summary = lldb.SBTypeSummary.CreateWithSummaryString('${var.width} x ${var.height}')
"
) lldb::SBTypeSummary::CreateWithSummaryString;

%feature("docstring",
"Creates a summary that calls a Python function.

``data`` is the name of a function that is reachable from the interpreter, e.g.
``my_module.task_summary``. The function takes an `SBValue`, the internal
dictionary and an `SBTypeSummaryOptions`, and returns the summary string."
) lldb::SBTypeSummary::CreateWithFunctionName;

%feature("docstring",
"Creates a summary from Python source code.

The code is the body of a summary function; use
`SBTypeSummary.CreateWithFunctionName` for a function that lives in a module."
) lldb::SBTypeSummary::CreateWithScriptCode;

%feature("docstring",
"Creates a summary that is implemented by a Python class.

``data`` is the name of a class whose instances produce the summary."
) lldb::SBTypeSummary::CreateWithClassName;

%feature("docstring",
"Creates a summary that calls a C++ callback.

The callback takes an `SBValue`, an `SBTypeSummaryOptions` and the `SBStream` to
write the summary to. Only useful from C++."
) lldb::SBTypeSummary::CreateWithCallback;

%feature("docstring",
"Returns whether this object refers to a summary."
) lldb::SBTypeSummary::IsValid;

%feature("docstring",
"Returns whether this summary is given as Python source code."
) lldb::SBTypeSummary::IsFunctionCode;

%feature("docstring",
"Returns whether this summary is given as the name of a Python function."
) lldb::SBTypeSummary::IsFunctionName;

%feature("docstring",
"Returns whether this summary is given as a summary format string."
) lldb::SBTypeSummary::IsSummaryString;

%feature("docstring",
"Returns the summary string, the function name or the source code of this
summary.

Which of the three it is depends on
`SBTypeSummary.IsSummaryString`, `SBTypeSummary.IsFunctionName` and
`SBTypeSummary.IsFunctionCode`."
) lldb::SBTypeSummary::GetData;

%feature("docstring",
"Turns this into a summary that uses the given summary format string."
) lldb::SBTypeSummary::SetSummaryString;

%feature("docstring",
"Turns this into a summary that calls the Python function with the given name."
) lldb::SBTypeSummary::SetFunctionName;

%feature("docstring",
"Turns this into a summary that is implemented by the given Python source
code."
) lldb::SBTypeSummary::SetFunctionCode;

%feature("docstring",
"Returns how many levels of pointers this summary applies through.

A depth of ``1`` means the summary is also used for a pointer to the type, a
depth of ``0`` that it only applies to the type itself."
) lldb::SBTypeSummary::GetPtrMatchDepth;

%feature("docstring",
"Sets how many levels of pointers this summary applies through, see
`SBTypeSummary.GetPtrMatchDepth`."
) lldb::SBTypeSummary::SetPtrMatchDepth;

%feature("docstring",
"Returns the options of this summary as a bit mask of the
``lldb.eTypeOption*`` values."
) lldb::SBTypeSummary::GetOptions;

%feature("docstring",
"Sets the options of this summary.

``options`` is a bit mask of the ``lldb.eTypeOption*`` values, for example
``lldb.eTypeOptionCascade`` to also apply the summary to derived types or
``lldb.eTypeOptionHideChildren`` to only show the summary."
) lldb::SBTypeSummary::SetOptions;

%feature("docstring",
"Writes a description of this summary into the given `SBStream`."
) lldb::SBTypeSummary::GetDescription;

%feature("docstring",
"Returns whether the value itself is printed in addition to the summary."
) lldb::SBTypeSummary::DoesPrintValue;

%feature("docstring",
"Returns whether this summary is the same as another one."
) lldb::SBTypeSummary::IsEqualTo;
