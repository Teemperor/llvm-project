%feature("docstring",
"Represents a synthetic children provider for one or more types.

A synthetic children provider is a Python class that computes what the children
of a value look like. This is how LLDB shows the elements of a ``std::vector`` or
the entries of a dictionary instead of the internal fields of those types. It is
the API equivalent of the ``type synthetic add`` command and it is registered with
`SBTypeCategory.AddTypeSynthetic`::

    # my_module.py contains a class 'TaskListProvider' with the methods
    # num_children(), get_child_at_index(), get_child_index() and update().
    synth = lldb.SBTypeSynthetic.CreateWithClassName('my_module.TaskListProvider')
    debugger.GetDefaultCategory().AddTypeSynthetic(lldb.SBTypeNameSpecifier('TaskList'),
                                                   synth)

The values a provider hands out are usually created with
`SBValue.CreateValueFromExpression`, `SBValue.CreateChildAtOffset` or
`SBValue.CreateValueFromData`, and `SBValue.GetNonSyntheticValue` bypasses the
provider to see the real members of an object.

See :doc:`/use/variable` for the interface such a class has to implement and
`SBTypeFilter` for a simpler way to just hide some children."
) lldb::SBTypeSynthetic;

%feature("docstring",
"Creates a synthetic children provider from the name of a Python class.

The class has to be reachable from the interpreter, e.g.
``my_module.MyProvider`` after ``my_module`` was imported. ``options`` is a bit
mask of the ``lldb.eTypeOption*`` values."
) lldb::SBTypeSynthetic::CreateWithClassName;

%feature("docstring",
"Creates a synthetic children provider from Python source code.

The code is the body of a class definition; use
`SBTypeSynthetic.CreateWithClassName` for a class that lives in a module."
) lldb::SBTypeSynthetic::CreateWithScriptCode;

%feature("docstring",
"Returns whether this object refers to a synthetic children provider."
) lldb::SBTypeSynthetic::IsValid;

%feature("docstring",
"Returns whether this provider is given as Python source code."
) lldb::SBTypeSynthetic::IsClassCode;

%feature("docstring",
"Returns whether this provider is given as the name of a Python class."
) lldb::SBTypeSynthetic::IsClassName;

%feature("docstring",
"Returns the class name or the source code of this provider.

Which of the two it is depends on `SBTypeSynthetic.IsClassName` and
`SBTypeSynthetic.IsClassCode`."
) lldb::SBTypeSynthetic::GetData;

%feature("docstring",
"Sets the name of the Python class that implements this provider."
) lldb::SBTypeSynthetic::SetClassName;

%feature("docstring",
"Sets the Python source code that implements this provider."
) lldb::SBTypeSynthetic::SetClassCode;

%feature("docstring",
"Returns the options of this provider as a bit mask of the
``lldb.eTypeOption*`` values."
) lldb::SBTypeSynthetic::GetOptions;

%feature("docstring",
"Sets the options of this provider.

``options`` is a bit mask of the ``lldb.eTypeOption*`` values, for example
``lldb.eTypeOptionCascade`` to also apply the provider to derived types."
) lldb::SBTypeSynthetic::SetOptions;

%feature("docstring",
"Writes a description of this provider into the given `SBStream`."
) lldb::SBTypeSynthetic::GetDescription;

%feature("docstring",
"Returns whether this provider is the same as another one."
) lldb::SBTypeSynthetic::IsEqualTo;
