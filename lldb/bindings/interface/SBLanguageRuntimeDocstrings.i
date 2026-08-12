%feature("docstring",
"Provides information about the programming languages LLDB knows about.

All functions of this class are static and operate on the
``lldb.eLanguageType*`` enumerators::

    print(lldb.SBLanguageRuntime.GetNameForLanguageType(frame.GuessLanguage()))
    language = lldb.SBLanguageRuntime.GetLanguageTypeFromString('c++')
"
) lldb::SBLanguageRuntime;

%feature("docstring",
"Returns the ``lldb.eLanguageType*`` enumerator for a language name.

The names are the ones the command line interface accepts, e.g. ``c++``,
``objective-c`` or ``swift``. Returns ``lldb.eLanguageTypeUnknown`` for an
unknown name."
) lldb::SBLanguageRuntime::GetLanguageTypeFromString;

%feature("docstring",
"Returns the name of a language as a string.

This is the inverse of
`SBLanguageRuntime.GetLanguageTypeFromString`."
) lldb::SBLanguageRuntime::GetNameForLanguageType;

%feature("docstring",
"Returns whether the given language is one of the C++ dialects."
) lldb::SBLanguageRuntime::LanguageIsCPlusPlus;

%feature("docstring",
"Returns whether the given language is one of the Objective-C dialects."
) lldb::SBLanguageRuntime::LanguageIsObjC;

%feature("docstring",
"Returns whether the given language belongs to the C family.

This includes C, C++ and Objective-C."
) lldb::SBLanguageRuntime::LanguageIsCFamily;

%feature("docstring",
"Returns whether exception breakpoints can stop when an exception is thrown.

See `SBTarget.BreakpointCreateForException`."
) lldb::SBLanguageRuntime::SupportsExceptionBreakpointsOnThrow;

%feature("docstring",
"Returns whether exception breakpoints can stop when an exception is caught.

See `SBTarget.BreakpointCreateForException`."
) lldb::SBLanguageRuntime::SupportsExceptionBreakpointsOnCatch;

%feature("docstring",
"Returns the keyword this language uses to throw an exception, e.g. ``throw``."
) lldb::SBLanguageRuntime::GetThrowKeywordForLanguage;

%feature("docstring",
"Returns the keyword this language uses to catch an exception, e.g. ``catch``."
) lldb::SBLanguageRuntime::GetCatchKeywordForLanguage;
