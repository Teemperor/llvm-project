%feature("docstring",
"Describes which variables `SBFrame.GetVariables` should return.

This is the more expressive alternative to the boolean parameters of
`SBFrame.GetVariables`::

    options = lldb.SBVariablesOptions()
    options.SetIncludeArguments(True)
    options.SetIncludeLocals(True)
    options.SetInScopeOnly(True)
    options.SetUseDynamic(lldb.eDynamicCanRunTarget)
    for value in frame.GetVariables(options):
        print('%s = %s' % (value.GetName(), value.GetValue()))
"
) lldb::SBVariablesOptions;

%feature("docstring",
"Returns whether this object holds a set of options."
) lldb::SBVariablesOptions::IsValid;

%feature("docstring",
"Returns whether the arguments of the function are included."
) lldb::SBVariablesOptions::GetIncludeArguments;

%feature("docstring",
"Sets whether the arguments of the function are included."
) lldb::SBVariablesOptions::SetIncludeArguments;

%feature("docstring",
"Returns whether the arguments a frame recognizer provides are included.

Frame recognizers can describe the arguments of a function that has no debug
information, for example for well known system functions."
) lldb::SBVariablesOptions::GetIncludeRecognizedArguments;

%feature("docstring",
"Sets whether the arguments a frame recognizer provides are included.

See `SBVariablesOptions.GetIncludeRecognizedArguments`."
) lldb::SBVariablesOptions::SetIncludeRecognizedArguments;

%feature("docstring",
"Returns whether local variables are included."
) lldb::SBVariablesOptions::GetIncludeLocals;

%feature("docstring",
"Sets whether local variables are included."
) lldb::SBVariablesOptions::SetIncludeLocals;

%feature("docstring",
"Returns whether static and global variables are included."
) lldb::SBVariablesOptions::GetIncludeStatics;

%feature("docstring",
"Sets whether static and global variables are included."
) lldb::SBVariablesOptions::SetIncludeStatics;

%feature("docstring",
"Returns whether the values use their synthetic children providers.

See `SBValue.GetNonSyntheticValue`."
) lldb::SBVariablesOptions::GetIncludeSynthetic;

%feature("docstring",
"Sets whether the values use their synthetic children providers."
) lldb::SBVariablesOptions::SetIncludeSynthetic;

%feature("docstring",
"Returns whether variables that are not in scope at the current program counter
are skipped.

See `SBValue.IsInScope`."
) lldb::SBVariablesOptions::GetInScopeOnly;

%feature("docstring",
"Sets whether variables that are not in scope at the current program counter are
skipped."
) lldb::SBVariablesOptions::SetInScopeOnly;

%feature("docstring",
"Returns whether the artificial variables of language runtimes are included.

See `SBValue.IsRuntimeSupportValue`."
) lldb::SBVariablesOptions::GetIncludeRuntimeSupportValues;

%feature("docstring",
"Sets whether the artificial variables of language runtimes are included."
) lldb::SBVariablesOptions::SetIncludeRuntimeSupportValues;

%feature("docstring",
"Returns whether the values use their dynamic type.

The result is one of the ``lldb.eDynamic*`` enumerators, see
`SBValue.GetDynamicValue`."
) lldb::SBVariablesOptions::GetUseDynamic;

%feature("docstring",
"Sets whether the values use their dynamic type.

``use_dynamic`` is one of the ``lldb.eDynamic*`` enumerators."
) lldb::SBVariablesOptions::SetUseDynamic;
