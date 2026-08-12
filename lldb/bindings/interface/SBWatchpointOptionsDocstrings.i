%feature("docstring",
"A container for options to use when creating watchpoints.

Pass it to `SBTarget.WatchpointCreateByAddress` to decide what kind of memory
accesses the watchpoint should stop on::

    options = lldb.SBWatchpointOptions()
    options.SetWatchpointTypeWrite(lldb.eWatchpointWriteTypeOnModify)
    error = lldb.SBError()
    watchpoint = target.WatchpointCreateByAddress(addr, 4, options, error)

See also :py:class:`SBWatchpoint`."
) lldb::SBWatchpointOptions;

%feature("docstring", "Sets whether the watchpoint should stop on read accesses."
) lldb::SBWatchpointOptions::SetWatchpointTypeRead;
%feature("docstring", "Gets whether the watchpoint should stop on read accesses."
) lldb::SBWatchpointOptions::GetWatchpointTypeRead;
%feature("docstring", "Sets whether the watchpoint should stop on write accesses. eWatchpointWriteTypeOnModify is the most commonly useful mode, where lldb will stop when the watched value has changed. eWatchpointWriteTypeAlways will stop on any write to the watched region, even if it's the value is the same."
) lldb::SBWatchpointOptions::SetWatchpointTypeWrite;
%feature("docstring", "Gets whether the watchpoint should stop on write accesses, returning WatchpointWriteType to indicate the type of write watching that is enabled, or eWatchpointWriteTypeDisabled."
) lldb::SBWatchpointOptions::GetWatchpointTypeWrite;
