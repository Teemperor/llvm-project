%feature("docstring",
"Represents a filter that can be associated to one or more types.

A filter hides the children of a type except for the ones that are listed in it,
which is useful for classes with many members of which only a few matter. It is
the API equivalent of the ``type filter add`` command and it is registered with
`SBTypeCategory.AddTypeFilter`::

    # Only show the 'id' and 'next' members of a Task.
    filter = lldb.SBTypeFilter(0)
    filter.AppendExpressionPath('id')
    filter.AppendExpressionPath('next')
    debugger.GetDefaultCategory().AddTypeFilter(lldb.SBTypeNameSpecifier('Task'), filter)

The children are given as expression paths, so members of members can be listed
as well (e.g. ``inner.value``). See `SBTypeSynthetic` for computing children
programmatically instead, and :doc:`/use/variable` for the data formatter system
as a whole."
) lldb::SBTypeFilter;

%feature("docstring",
"Returns whether this object refers to a filter."
) lldb::SBTypeFilter::IsValid;

%feature("docstring",
"Returns the number of children this filter shows."
) lldb::SBTypeFilter::GetNumberOfExpressionPaths;

%feature("docstring",
"Returns the expression path of the child at the given index."
) lldb::SBTypeFilter::GetExpressionPathAtIndex;

%feature("docstring",
"Replaces the expression path at the given index.

Returns whether the index was valid."
) lldb::SBTypeFilter::ReplaceExpressionPathAtIndex;

%feature("docstring",
"Adds a child to show, given as an expression path such as ``id`` or
``inner.value``."
) lldb::SBTypeFilter::AppendExpressionPath;

%feature("docstring",
"Removes all children from this filter."
) lldb::SBTypeFilter::Clear;

%feature("docstring",
"Returns the options of this filter as a bit mask of the
``lldb.eTypeOption*`` values."
) lldb::SBTypeFilter::GetOptions;

%feature("docstring",
"Sets the options of this filter.

``options`` is a bit mask of the ``lldb.eTypeOption*`` values, for example
``lldb.eTypeOptionCascade`` to also apply the filter to derived types."
) lldb::SBTypeFilter::SetOptions;

%feature("docstring",
"Writes a description of this filter into the given `SBStream`."
) lldb::SBTypeFilter::GetDescription;

%feature("docstring",
"Returns whether this filter is the same as another one."
) lldb::SBTypeFilter::IsEqualTo;
