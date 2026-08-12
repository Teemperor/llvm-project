%feature("docstring",
"Allows you to manipulate LLDB\'s signal disposition.

For every signal of the target\'s operating system, LLDB decides what to do when
the debugged process receives it: whether to stop the process, whether to tell
the user about it and whether the signal is delivered to the process at all.
This is what the ``process handle`` command changes.

Unix signals are obtained from a running process
(`SBProcess.GetUnixSignals`) or from a platform
(`SBPlatform.GetUnixSignals`)::

    signals = process.GetUnixSignals()
    # Don\'t stop when the process receives SIGPIPE, but pass it through.
    signo = signals.GetSignalNumberFromName('SIGPIPE')
    signals.SetShouldStop(signo, False)
    signals.SetShouldNotify(signo, False)
    signals.SetShouldSuppress(signo, False)

A thread that stopped because of a signal reports
``lldb.eStopReasonSignal``, and the signal number is available through
`SBThread.GetStopReasonDataAtIndex`."
) lldb::SBUnixSignals;

%feature("docstring",
"Resets this object to an invalid state."
) lldb::SBUnixSignals::Clear;

%feature("docstring",
"Returns whether this object refers to the signals of a process or platform."
) lldb::SBUnixSignals::IsValid;

%feature("docstring",
"Returns the name of the signal with the given number, e.g. ``SIGSEGV``.

Returns ``None`` if the signal number is unknown."
) lldb::SBUnixSignals::GetSignalAsCString;

%feature("docstring",
"Returns the number of the signal with the given name.

Returns ``lldb.LLDB_INVALID_SIGNAL_NUMBER`` if there is no signal with that
name::

    signo = signals.GetSignalNumberFromName('SIGINT')
"
) lldb::SBUnixSignals::GetSignalNumberFromName;

%feature("docstring",
"Returns whether the signal is suppressed, i.e. not delivered to the process.

See `SBUnixSignals.SetShouldSuppress`."
) lldb::SBUnixSignals::GetShouldSuppress;

%feature("docstring",
"Sets whether the signal is suppressed instead of delivered to the process.

A suppressed signal never reaches the debugged process, which is useful for
signals a debugger causes itself. Returns whether the change was applied."
) lldb::SBUnixSignals::SetShouldSuppress;

%feature("docstring",
"Returns whether the process stops when it receives the signal.

See `SBUnixSignals.SetShouldStop`."
) lldb::SBUnixSignals::GetShouldStop;

%feature("docstring",
"Sets whether the process stops when it receives the signal.

If this is ``False`` the process keeps running and the signal is handled by the
program itself. Returns whether the change was applied."
) lldb::SBUnixSignals::SetShouldStop;

%feature("docstring",
"Returns whether LLDB tells the user about the signal.

See `SBUnixSignals.SetShouldNotify`."
) lldb::SBUnixSignals::GetShouldNotify;

%feature("docstring",
"Sets whether LLDB prints a message when the process receives the signal.

Returns whether the change was applied."
) lldb::SBUnixSignals::SetShouldNotify;

%feature("docstring",
"Returns the number of signals this object knows about.

See `SBUnixSignals.GetSignalAtIndex`; in Python, iterating over an
SBUnixSignals object yields all signal numbers."
) lldb::SBUnixSignals::GetNumSignals;

%feature("docstring",
"Returns the signal number at the given index.

Note that this is an index into the list of known signals, not the signal number
itself. Returns ``lldb.LLDB_INVALID_SIGNAL_NUMBER`` if the index is out of
bounds."
) lldb::SBUnixSignals::GetSignalAtIndex;
