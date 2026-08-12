%feature("docstring",
 "A read-only tree of data: dictionaries, arrays, strings, numbers and booleans.

Structured data is LLDB\'s way of handing out information whose shape is not
fixed by the API. It is used for statistics (`SBTarget.GetStatistics`), for the
data of some events (`SBProcess.GetStructuredDataFromEvent`,
`SBDebugger.GetProgressDataFromEvent`), for the settings of a debugger
(`SBDebugger.GetSetting`) and to pass parameters to scripted breakpoints,
processes and thread plans.

`SBStructuredData.GetType` says what a node is, and depending on that either
`SBStructuredData.GetValueForKey` (dictionaries),
`SBStructuredData.GetItemAtIndex` (arrays) or one of the ``Get*Value``
functions reads it::

    stats = target.GetStatistics()
    modules = stats.GetValueForKey('modules')
    for i in range(modules.GetSize()):
        module = modules.GetItemAtIndex(i)
        print(module.GetValueForKey('path').GetStringValue(1024))

A whole tree can be created from JSON with
`SBStructuredData.SetFromJSON` and written back out with
`SBStructuredData.GetAsJSON`, which is often the most convenient way to work
with it in Python::

    data = lldb.SBStructuredData()
    data.SetFromJSON(\'{\"threshold\": 42}\')
    print(data.GetValueForKey('threshold').GetUnsignedIntegerValue())
"
) lldb::SBStructuredData;

%feature("docstring",
"Returns whether this object holds any data."
) lldb::SBStructuredData::IsValid;

%feature("docstring",
"Fills this object from JSON, given either as a string or in an `SBStream`.

Returns an `SBError` describing any parse error::

    error = data.SetFromJSON(\'{\"key\": [1, 2, 3]}\')
"
) lldb::SBStructuredData::SetFromJSON;

%feature("docstring",
"Resets this object to an invalid, empty state."
) lldb::SBStructuredData::Clear;

%feature("docstring",
"Writes this data as JSON into the given `SBStream`.

Returns an `SBError` describing any failure::

    stream = lldb.SBStream()
    data.GetAsJSON(stream)
    parsed = json.loads(stream.GetData())
"
) lldb::SBStructuredData::GetAsJSON;

%feature("docstring",
"Writes a human readable description of this data into the given `SBStream`."
) lldb::SBStructuredData::GetDescription;

%feature("docstring",
"Returns what kind of node this is.

The result is one of the ``lldb.eStructuredDataType*`` enumerators, e.g.
``lldb.eStructuredDataTypeDictionary``, ``lldb.eStructuredDataTypeArray`` or
``lldb.eStructuredDataTypeInteger``. It decides which of the accessors below
returns anything useful."
) lldb::SBStructuredData::GetType;

%feature("docstring",
"Returns the number of elements of an array or dictionary node.

Returns ``0`` for all other kinds of nodes."
) lldb::SBStructuredData::GetSize;

%feature("docstring",
"Fills the given `SBStringList` with the keys of a dictionary node.

Returns ``False`` if this node is not a dictionary."
) lldb::SBStructuredData::GetKeys;

%feature("docstring",
"Returns the value of a dictionary node for the given key.

Returns invalid structured data if this node is not a dictionary or has no such
key."
) lldb::SBStructuredData::GetValueForKey;

%feature("docstring",
"Returns the element of an array node at the given index.

Returns invalid structured data if this node is not an array or the index is out
of bounds."
) lldb::SBStructuredData::GetItemAtIndex;

%feature("docstring",
"Returns the value of an unsigned integer node.

``fail_value`` is returned if this node is not an integer."
) lldb::SBStructuredData::GetUnsignedIntegerValue;

%feature("docstring",
"Returns the value of a signed integer node.

``fail_value`` is returned if this node is not an integer."
) lldb::SBStructuredData::GetSignedIntegerValue;

%feature("docstring",
"Deprecated, use `SBStructuredData.GetUnsignedIntegerValue` or
`SBStructuredData.GetSignedIntegerValue`."
) lldb::SBStructuredData::GetIntegerValue;

%feature("docstring",
"Returns the value of a floating point node.

``fail_value`` is returned if this node is not a floating point number."
) lldb::SBStructuredData::GetFloatValue;

%feature("docstring",
"Returns the value of a boolean node.

``fail_value`` is returned if this node is not a boolean."
) lldb::SBStructuredData::GetBooleanValue;

%feature("docstring",
"Returns the value of a string node.

In Python this takes the maximum length of the string and returns it::

    print(node.GetStringValue(1024))

Returns an empty string if this node is not a string."
) lldb::SBStructuredData::GetStringValue;

%feature("docstring",
"Returns the scripted object a generic node wraps as an `SBScriptObject`.

Generic nodes are how a Python object that LLDB does not understand is passed
through structured data unchanged."
) lldb::SBStructuredData::GetGenericValue;

%feature("docstring",
"Sets the value of the given key of a dictionary node.

Turns this node into a dictionary if it isn\'t one yet::

    value = lldb.SBStructuredData()
    value.SetStringValue('/tmp/log.txt')
    options.SetValueForKey('log_path', value)
"
) lldb::SBStructuredData::SetValueForKey;

%feature("docstring",
"Turns this node into an unsigned integer with the given value."
) lldb::SBStructuredData::SetUnsignedIntegerValue;

%feature("docstring",
"Turns this node into a signed integer with the given value."
) lldb::SBStructuredData::SetSignedIntegerValue;

%feature("docstring",
"Turns this node into a floating point number with the given value."
) lldb::SBStructuredData::SetFloatValue;

%feature("docstring",
"Turns this node into a boolean with the given value."
) lldb::SBStructuredData::SetBooleanValue;

%feature("docstring",
"Turns this node into a string with the given value."
) lldb::SBStructuredData::SetStringValue;

%feature("docstring",
"Turns this node into a generic node wrapping the given `SBScriptObject`."
) lldb::SBStructuredData::SetGenericValue;
