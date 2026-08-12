%feature("docstring",
"Represents a category that can contain formatters for types.

A category groups the data formatters that decide how LLDB displays values:
formats (`SBTypeFormat`), summaries (`SBTypeSummary`), filters
(`SBTypeFilter`) and synthetic children providers (`SBTypeSynthetic`).
Categories can be enabled and disabled as a whole, which is how the formatters
for a language or a library are turned on and off; see the ``type category``
commands.

Categories are obtained from a debugger with
`SBDebugger.GetCategory`, `SBDebugger.CreateCategory` and
`SBDebugger.GetDefaultCategory`. Adding a formatter to one associates it with
the types matched by an `SBTypeNameSpecifier`::

    category = debugger.CreateCategory('MyFormatters')
    summary = lldb.SBTypeSummary.CreateWithSummaryString('id = ${var.id}')
    category.AddTypeSummary(lldb.SBTypeNameSpecifier('Task'), summary)
    category.SetEnabled(True)

    # Now every value of type 'Task' has that summary.
    print(frame.FindVariable('task').GetSummary())

See :doc:`/use/variable` for a description of the data formatter system as a
whole."
) lldb::SBTypeCategory;

%feature("docstring",
"Returns whether this object refers to a category."
) lldb::SBTypeCategory::IsValid;

%feature("docstring",
"Returns whether the formatters of this category are used.

See `SBTypeCategory.SetEnabled`."
) lldb::SBTypeCategory::GetEnabled;

%feature("docstring",
"Enables or disables all formatters of this category at once.

A newly created category is disabled, so this has to be called for its
formatters to have any effect."
) lldb::SBTypeCategory::SetEnabled;

%feature("docstring",
"Returns the name of this category."
) lldb::SBTypeCategory::GetName;

%feature("docstring",
"Returns the language at the given index as an ``lldb.eLanguageType*``
enumerator.

A category can be restricted to a set of languages, in which case its formatters
only apply to values of those languages."
) lldb::SBTypeCategory::GetLanguageAtIndex;

%feature("docstring",
"Returns the number of languages this category applies to.

Returns ``0`` for a category that applies to all languages."
) lldb::SBTypeCategory::GetNumLanguages;

%feature("docstring",
"Restricts this category to another language.

``language`` is one of the ``lldb.eLanguageType*`` enumerators."
) lldb::SBTypeCategory::AddLanguage;

%feature("docstring",
"Writes a description of this category into the given `SBStream`."
) lldb::SBTypeCategory::GetDescription;

%feature("docstring",
"Returns the number of formats in this category, see
`SBTypeCategory.GetFormatAtIndex`."
) lldb::SBTypeCategory::GetNumFormats;

%feature("docstring",
"Returns the number of summaries in this category, see
`SBTypeCategory.GetSummaryAtIndex`."
) lldb::SBTypeCategory::GetNumSummaries;

%feature("docstring",
"Returns the number of filters in this category, see
`SBTypeCategory.GetFilterAtIndex`."
) lldb::SBTypeCategory::GetNumFilters;

%feature("docstring",
"Returns the number of synthetic children providers in this category, see
`SBTypeCategory.GetSyntheticAtIndex`."
) lldb::SBTypeCategory::GetNumSynthetics;

%feature("docstring",
"Returns the type names the filter at the given index applies to.

Together with `SBTypeCategory.GetFilterAtIndex` this makes it possible to walk
all filters of a category and see which types they are for.

:rtype: SBTypeNameSpecifier"
) lldb::SBTypeCategory::GetTypeNameSpecifierForFilterAtIndex;

%feature("docstring",
"Returns the type names the format at the given index applies to.

:rtype: SBTypeNameSpecifier"
) lldb::SBTypeCategory::GetTypeNameSpecifierForFormatAtIndex;

%feature("docstring",
"Returns the type names the summary at the given index applies to.

:rtype: SBTypeNameSpecifier"
) lldb::SBTypeCategory::GetTypeNameSpecifierForSummaryAtIndex;

%feature("docstring",
"Returns the type names the synthetic children provider at the given index
applies to.

:rtype: SBTypeNameSpecifier"
) lldb::SBTypeCategory::GetTypeNameSpecifierForSyntheticAtIndex;

%feature("docstring",
"Returns the filter of this category that applies to the given type names.

``spec`` is an `SBTypeNameSpecifier`. Returns an invalid `SBTypeFilter` if this
category has no filter for those types."
) lldb::SBTypeCategory::GetFilterForType;

%feature("docstring",
"Returns the format of this category that applies to the given type names.

See `SBTypeCategory.GetFilterForType`."
) lldb::SBTypeCategory::GetFormatForType;

%feature("docstring",
"Returns the summary of this category that applies to the given type names.

See `SBTypeCategory.GetFilterForType`."
) lldb::SBTypeCategory::GetSummaryForType;

%feature("docstring",
"Returns the synthetic children provider of this category that applies to the
given type names.

See `SBTypeCategory.GetFilterForType`."
) lldb::SBTypeCategory::GetSyntheticForType;

%feature("docstring",
"Returns the filter at the given index as an `SBTypeFilter`."
) lldb::SBTypeCategory::GetFilterAtIndex;

%feature("docstring",
"Returns the format at the given index as an `SBTypeFormat`."
) lldb::SBTypeCategory::GetFormatAtIndex;

%feature("docstring",
"Returns the summary at the given index as an `SBTypeSummary`."
) lldb::SBTypeCategory::GetSummaryAtIndex;

%feature("docstring",
"Returns the synthetic children provider at the given index as an
`SBTypeSynthetic`."
) lldb::SBTypeCategory::GetSyntheticAtIndex;

%feature("docstring",
"Adds a format for the types matched by the given `SBTypeNameSpecifier`.

Returns whether the format was added::

    fmt = lldb.SBTypeFormat(lldb.eFormatHex)
    category.AddTypeFormat(lldb.SBTypeNameSpecifier('MyHandle'), fmt)
"
) lldb::SBTypeCategory::AddTypeFormat;

%feature("docstring",
"Removes the format for the types matched by the given `SBTypeNameSpecifier`.

Returns whether such a format existed."
) lldb::SBTypeCategory::DeleteTypeFormat;

%feature("docstring",
"Adds a summary for the types matched by the given `SBTypeNameSpecifier`.

Returns whether the summary was added, see `SBTypeSummary`."
) lldb::SBTypeCategory::AddTypeSummary;

%feature("docstring",
"Removes the summary for the types matched by the given `SBTypeNameSpecifier`.

Returns whether such a summary existed."
) lldb::SBTypeCategory::DeleteTypeSummary;

%feature("docstring",
"Adds a filter for the types matched by the given `SBTypeNameSpecifier`.

Returns whether the filter was added, see `SBTypeFilter`."
) lldb::SBTypeCategory::AddTypeFilter;

%feature("docstring",
"Removes the filter for the types matched by the given `SBTypeNameSpecifier`.

Returns whether such a filter existed."
) lldb::SBTypeCategory::DeleteTypeFilter;

%feature("docstring",
"Adds a synthetic children provider for the types matched by the given
`SBTypeNameSpecifier`.

Returns whether the provider was added, see `SBTypeSynthetic`."
) lldb::SBTypeCategory::AddTypeSynthetic;

%feature("docstring",
"Removes the synthetic children provider for the types matched by the given
`SBTypeNameSpecifier`.

Returns whether such a provider existed."
) lldb::SBTypeCategory::DeleteTypeSynthetic;
