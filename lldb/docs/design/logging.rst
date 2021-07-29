Logging
=======

LLDB can create logs that describe events occuring during a debug session. These
logs are intended to be used as an aid when trying to understand LLDB's
behaviour from a developer perspective. Users are usually not expected to
enable or read logs.

LLDB's logging framework organizes logs into channels each identified by
a name (e.g., ``lldb``, ```dwarf```, etc.). Each channel is further divided
into several categories which are also identified by a name (e.g., the``api``
category in the ``lldb`` channel is used to log information about SB API calls).

Each log message is associated with a specific category in a specific channel.
A message is only emitted when their respective category is enabled.

There are two ways to enable and disable logging in lldb. The ``log``
command offers the ``enable`` and ``disable`` subcommands which take a channel
and a list of categories to enable/disable. From the SB API the
`SBDebugger.EnableLog` function can also be used to enable a log channel and
category.

LLDB allows configuring format that log messages are emitted in and what
metainformation should be attached to each log message. These options can
be set for each logging channel (but not separately for each logging category).
The output of each channel can also be directed to write to a log file,
to the standard output of the LLDB process or a logging callback specified
by the user (when using `SBDebugger.EnableLog`).

Supported log formats in LLDB are plain text and JSON. The metainformation
that can be added to log messages includes timestamps, LLDB's own pid, or the
backtraces of where the log message was generated. See the options of the
``log enable`` command for a full list of ways to configure a logging channel.

JSON log format
---------------

While the plain text log format is intended to be human-readable, the JSON
log format is only intended to be read by other programs. These other programs
can post-process the log files to filter them or present them in a more
sophisticated way such as a table.

The JSON log format used by LLDB to log messages is not expected to change, but
it is also not guaranteed to be stable across LLDB releases. Users should not
rely on the format to be stable.

The current JSON log format puts each log message in its own JSON object. Each
object also contains a trailing comma character (``,``). The intended way of
parsing a JSON log file is strip the last trailing comma character and then
surround the whole contents with brackets (``[]``). This way the list of
JSON objects printed by LLDB will form one single top-level JSON array object
that a normal JSON parser can parse. The listing below contains an example log
file in the JSON format.

::

    {"timestamp":24028401.002, "message":"A log message"},
    {"timestamp":24028401.004, "message":"Another log message"},


Each JSON message object can contain several fields. The only mandatory field
in a message object is the ``message`` field. All other fields are optional
and are only created when the respective option in their logging channel is
set.

.. list-table:: JSON message fields
   :header-rows: 1
   :widths: 1 1 10

   * - Field name
     - JavaScript Type
     - Description
   * - ``message``
     - String
     - The log message itself.
   * - ``sequence-id``
     - Number
     - A number uniquely identifying a log message within one debugging session.
   * - ``timestamp``
     - Number
     - The number of seconds since the host systems epoch (usually UNIX time).
   * - ``pid``
     - Number
     - The process id of the LLDB process itself.
   * - ``tid``
     - Number
     - The thread id of the thread in the LLDB process that generated the log message.
   * - ``thread-name``
     - String
     - The host specific name of the thread that generated the log message.
   * - ``stacktrace``
     - String
     - A textual representation of the stacktrace to where the log message was generated from.
   * - ``function``
     - String
     - The name of the function within the log message is generated.
   * - ``file``
     - String
     - The path to the LLDB source file where the log message is generated.
