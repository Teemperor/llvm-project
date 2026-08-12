%feature("docstring",
"A container for options to use when dumping statistics.

Pass one to `SBTarget.GetStatistics` to control how much detail the statistics
contain::

    options = lldb.SBStatisticsOptions()
    options.SetSummaryOnly(True)
    stats = target.GetStatistics(options)

This is the API version of the options of the ``statistics dump`` command."
) lldb::SBStatisticsOptions;

%feature("docstring", "Sets whether the statistics should only dump a summary."
) lldb::SBStatisticsOptions::SetSummaryOnly;
%feature("docstring", "Gets whether the statistics only dump a summary."
) lldb::SBStatisticsOptions::GetSummaryOnly;
%feature("docstring", "
    Sets whether the statistics will force loading all possible debug info."
) lldb::SBStatisticsOptions::SetReportAllAvailableDebugInfo;
%feature("docstring", "
    Gets whether the statistics will force loading all possible debug info."
) lldb::SBStatisticsOptions::GetReportAllAvailableDebugInfo;

%feature("docstring", "
    Sets whether the statistics of the debugger\'s targets are included."
) lldb::SBStatisticsOptions::SetIncludeTargets;

%feature("docstring", "
    Gets whether the statistics of the debugger\'s targets are included."
) lldb::SBStatisticsOptions::GetIncludeTargets;

%feature("docstring", "
    Sets whether per-module statistics are included.

    Module statistics can be sizable for a program with many shared libraries,
    since they contain one entry per module."
) lldb::SBStatisticsOptions::SetIncludeModules;

%feature("docstring", "
    Gets whether per-module statistics are included."
) lldb::SBStatisticsOptions::GetIncludeModules;

%feature("docstring", "
    Sets whether the transcript of the commands that were run is included.

    The transcript is only available if the ``interpreter.save-transcript``
    setting is enabled, see `SBCommandInterpreter.GetTranscript`."
) lldb::SBStatisticsOptions::SetIncludeTranscript;

%feature("docstring", "
    Gets whether the transcript of the commands that were run is included."
) lldb::SBStatisticsOptions::GetIncludeTranscript;
