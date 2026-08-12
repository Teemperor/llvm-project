%feature("docstring",
"A message that a broadcaster sent to the listeners that subscribed to it.

Events are how LLDB reports asynchronous state changes: a process stopped or
exited, a breakpoint's locations changed, a module was loaded, progress is being
made on a long running operation. An event carries the `SBBroadcaster` that sent
it (`SBEvent.GetBroadcaster`), a type that is one bit of that broadcaster's event
bits (`SBEvent.GetType`) and data whose meaning depends on the broadcaster.

The typical pattern is to create an `SBListener`, subscribe it to the events of a
broadcaster and then wait for events in a loop::

    listener = lldb.SBListener('my listener')
    process.GetBroadcaster().AddListener(listener,
                                         lldb.SBProcess.eBroadcastBitStateChanged)
    event = lldb.SBEvent()
    while listener.WaitForEvent(1, event):
        if lldb.SBProcess.EventIsProcessEvent(event):
            state = lldb.SBProcess.GetStateFromEvent(event)
            print('process is now %s' % lldb.SBDebugger.StateAsCString(state))
            if state == lldb.eStateExited:
                break

The functions that make sense of an event's data live on the class that sent it,
for example `SBProcess.GetStateFromEvent`,
`SBBreakpoint.GetBreakpointEventTypeFromEvent`,
`SBTarget.GetModuleAtIndexFromEvent` and
`SBDebugger.GetProgressFromEvent`. Events are only delivered in asynchronous
mode, see `SBDebugger.SetAsync`.

For example, check out the following output: ::

    Try wait for event...
    Event description: 0x103d0bb70 Event: broadcaster = 0x1009c8410, type = 0x00000001, data = { process = 0x1009c8400 (pid = 21528), state = running}
    Event data flavor: Process::ProcessEventData
    Process state: running

    Try wait for event...
    Event description: 0x103a700a0 Event: broadcaster = 0x1009c8410, type = 0x00000001, data = { process = 0x1009c8400 (pid = 21528), state = stopped}
    Event data flavor: Process::ProcessEventData
    Process state: stopped

    Try wait for event...
    Event description: 0x103d0d4a0 Event: broadcaster = 0x1009c8410, type = 0x00000001, data = { process = 0x1009c8400 (pid = 21528), state = exited}
    Event data flavor: Process::ProcessEventData
    Process state: exited

    Try wait for event...
    timeout occurred waiting for event...

from test/python_api/event/TestEventspy: ::

    def do_listen_for_and_print_event(self):
        '''Create a listener and use SBEvent API to print the events received.'''
        exe = os.path.join(os.getcwd(), 'a.out')

        # Create a target by the debugger.
        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)

        # Now create a breakpoint on main.c by name 'c'.
        breakpoint = target.BreakpointCreateByName('c', 'a.out')

        # Now launch the process, and do not stop at the entry point.
        process = target.LaunchSimple(None, None, os.getcwd())
        self.assertTrue(process.GetState() == lldb.eStateStopped,
                        PROCESS_STOPPED)

        # Get a handle on the process's broadcaster.
        broadcaster = process.GetBroadcaster()

        # Create an empty event object.
        event = lldb.SBEvent()

        # Create a listener object and register with the broadcaster.
        listener = lldb.SBListener('my listener')
        rc = broadcaster.AddListener(listener, lldb.SBProcess.eBroadcastBitStateChanged)
        self.assertTrue(rc, 'AddListener successfully retruns')

        traceOn = self.TraceOn()
        if traceOn:
            lldbutil.print_stacktraces(process)

        # Create MyListeningThread class to wait for any kind of event.
        import threading
        class MyListeningThread(threading.Thread):
            def run(self):
                count = 0
                # Let's only try at most 4 times to retrieve any kind of event.
                # After that, the thread exits.
                while not count > 3:
                    if traceOn:
                        print('Try wait for event...')
                    if listener.WaitForEventForBroadcasterWithType(5,
                                                                   broadcaster,
                                                                   lldb.SBProcess.eBroadcastBitStateChanged,
                                                                   event):
                        if traceOn:
                            desc = lldbutil.get_description(event))
                            print('Event description:', desc)
                            print('Event data flavor:', event.GetDataFlavor())
                            print('Process state:', lldbutil.state_type_to_str(process.GetState()))
                            print()
                    else:
                        if traceOn:
                            print 'timeout occurred waiting for event...'
                    count = count + 1
                return

        # Let's start the listening thread to retrieve the events.
        my_thread = MyListeningThread()
        my_thread.start()

        # Use Python API to continue the process.  The listening thread should be
        # able to receive the state changed events.
        process.Continue()

        # Use Python API to kill the process.  The listening thread should be
        # able to receive the state changed event, too.
        process.Kill()

        # Wait until the 'MyListeningThread' terminates.
        my_thread.join()

See also :py:class:`SBListener` and :py:class:`SBBroadcaster`."
) lldb::SBEvent;

%feature("docstring",
"Returns whether this object holds an event.

A default constructed SBEvent is invalid until it is filled in by one of the
``WaitForEvent`` or ``GetNextEvent`` functions of `SBListener`."
) lldb::SBEvent::IsValid;

%feature("docstring",
"Returns the name of the kind of data this event carries.

This is a string such as ``Process::ProcessEventData`` that identifies which
class knows how to interpret the event's data."
) lldb::SBEvent::GetDataFlavor;

%feature("docstring",
"Returns the type of this event.

The type is one bit of the event bits of the broadcaster that sent the event,
e.g. ``lldb.SBProcess.eBroadcastBitStateChanged``::

    if event.GetType() == lldb.SBProcess.eBroadcastBitSTDOUT:
        print(process.GetSTDOUT(1024))
"
) lldb::SBEvent::GetType;

%feature("docstring",
"Returns the `SBBroadcaster` that sent this event."
) lldb::SBEvent::GetBroadcaster;

%feature("docstring",
"Returns the name of the class of the broadcaster that sent this event.

This is a string such as ``lldb.process``, which is what
`SBProcess.GetBroadcasterClassName` returns, and it is how the events of
different kinds of broadcasters can be told apart."
) lldb::SBEvent::GetBroadcasterClass;

%feature("docstring",
"Returns whether this event was sent by the given broadcaster."
) lldb::SBEvent::BroadcasterMatchesRef;

%feature("docstring",
"Resets this object to an invalid event."
) lldb::SBEvent::Clear;

%feature("docstring",
"Returns the string an event carries, if it carries one.

Only meaningful for events that were created with a string, such as the ones a
script sends with `SBBroadcaster.BroadcastEvent`. Returns ``None``
otherwise."
) lldb::SBEvent::GetCStringFromEvent;

%feature("docstring",
"Writes a description of this event into the given `SBStream`."
) lldb::SBEvent::GetDescription;

%feature("autodoc",
"__init__(self, int type, str data) -> SBEvent (make an event that contains a C string)"
) lldb::SBEvent::SBEvent;
