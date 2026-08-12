%feature("docstring",
"Allows sending/receiving data.

This class wraps a raw communication channel, such as a socket or a pipe to a
debug server. It is an implementation detail of LLDB\'s communication with
external processes; scripts that debug a program don\'t need it. Use
`SBProcess.PutSTDIN` and `SBProcess.GetSTDOUT` to talk to a debugged process
instead."
) lldb::SBCommunication;

%feature("docstring",
"Returns whether this object refers to a communication channel."
) lldb::SBCommunication::IsValid;

%feature("docstring",
"Returns the `SBBroadcaster` of this channel.

Its events report that data arrived or that the connection was closed."
) lldb::SBCommunication::GetBroadcaster;

%feature("docstring",
"Returns the name of the broadcaster class of communication channels."
) lldb::SBCommunication::GetBroadcasterClass;

%feature("docstring",
"Uses the given file descriptor for this channel.

``owns_fd`` says whether the descriptor is closed when this object is done with
it. Returns one of the ``lldb.eConnectionStatus*`` enumerators."
) lldb::SBCommunication::AdoptFileDesriptor;

%feature("docstring",
"Connects this channel to the given URL.

The URL is the same kind of string `SBTarget.ConnectRemote` takes, e.g.
``connect://localhost:1234``. Returns one of the ``lldb.eConnectionStatus*``
enumerators."
) lldb::SBCommunication::Connect;

%feature("docstring",
"Closes this channel and returns one of the ``lldb.eConnectionStatus*``
enumerators."
) lldb::SBCommunication::Disconnect;

%feature("docstring",
"Returns whether this channel is connected."
) lldb::SBCommunication::IsConnected;

%feature("docstring",
"Returns whether the channel is closed when the other side closes it."
) lldb::SBCommunication::GetCloseOnEOF;

%feature("docstring",
"Sets whether the channel is closed when the other side closes it."
) lldb::SBCommunication::SetCloseOnEOF;

%feature("docstring",
"Reads up to ``size`` bytes, waiting at most ``timeout_usec`` microseconds.

``status`` receives one of the ``lldb.eConnectionStatus*`` enumerators and the
return value is the number of bytes that were read."
) lldb::SBCommunication::Read;

%feature("docstring",
"Writes the given bytes to this channel.

``status`` receives one of the ``lldb.eConnectionStatus*`` enumerators and the
return value is the number of bytes that were written."
) lldb::SBCommunication::Write;

%feature("docstring",
"Starts a thread that reads from this channel in the background.

The data that is read is delivered through the callback that
`SBCommunication.SetReadThreadBytesReceivedCallback` installed. Returns whether
the thread was started."
) lldb::SBCommunication::ReadThreadStart;

%feature("docstring",
"Stops the background read thread, see
`SBCommunication.ReadThreadStart`."
) lldb::SBCommunication::ReadThreadStop;

%feature("docstring",
"Returns whether the background read thread is running."
) lldb::SBCommunication::ReadThreadIsRunning;

%feature("docstring",
"Installs the callback that receives the data of the background read thread.

See `SBCommunication.ReadThreadStart`."
) lldb::SBCommunication::SetReadThreadBytesReceivedCallback;
