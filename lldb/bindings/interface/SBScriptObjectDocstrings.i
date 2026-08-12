%feature("docstring",
"Wraps an object of a scripting language, such as a Python object.

Script objects appear where LLDB hands a scripted implementation back to a
script: `SBValue.GetTypeSyntheticImplementation` returns the synthetic children
provider of a value and `SBProcess.GetScriptedImplementation` the implementation
of a scripted process. They also carry the generic values of an
`SBStructuredData` (`SBStructuredData.GetGenericValue`).

The object itself is only accessible from the scripting language it belongs to;
from the API side `SBScriptObject.GetLanguage` says which language that is and
`SBScriptObject.GetPointer` is its opaque address."
) lldb::SBScriptObject;

%feature("docstring",
"Returns whether this object wraps a scripting language object."
) lldb::SBScriptObject::IsValid;

%feature("docstring",
"Returns the address of the wrapped object as an opaque pointer."
) lldb::SBScriptObject::GetPointer;

%feature("docstring",
"Returns the scripting language of the wrapped object.

The result is one of the ``lldb.eScriptLanguage*`` enumerators."
) lldb::SBScriptObject::GetLanguage;
