%feature("docstring",
"Receives the events of the broadcasters it is subscribed to.

A listener is the client side of LLDB\'s event system: it is subscribed to one or
more broadcasters (`SBBroadcaster.AddListener`,
`SBListener.StartListeningForEvents`) and then waits for their events
(`SBListener.WaitForEvent`).

Every `SBDebugger` has a listener (`SBDebugger.GetListener`) that receives the
events of all of its processes unless a different listener was passed to the
launch or attach (see `SBLaunchInfo.SetListener`). A listener can be given to
the launch functions of `SBTarget` or created and subscribed explicitly::

    listener = lldb.SBListener('my listener')
    process.GetBroadcaster().AddListener(listener,
                                         lldb.SBProcess.eBroadcastBitStateChanged)

    event = lldb.SBEvent()
    while listener.WaitForEvent(1, event):
        state = lldb.SBProcess.GetStateFromEvent(event)
        if state == lldb.eStateExited:
            break

Note that events are only delivered while the debugger is in asynchronous mode,
which is the default, see `SBDebugger.SetAsync`.

See also :py:class:`SBEvent` for example usage of creating and adding a listener."
) lldb::SBListener;

%feature("docstring",
"Returns whether this object refers to a listener."
) lldb::SBListener::IsValid;

%feature("docstring",
"Puts an event into this listener\'s queue.

The event is delivered to whoever waits on this listener next, which makes it
possible for a script to inject its own events."
) lldb::SBListener::AddEvent;

%feature("docstring",
"Unsubscribes this listener from all broadcasters and discards pending events."
) lldb::SBListener::Clear;

%feature("docstring",
"Subscribes this listener to a class of broadcasters of a debugger.

``broadcaster_class`` is the name of a broadcaster class, e.g. the value of
`SBProcess.GetBroadcasterClassName`, and ``event_mask`` the bits of the events to
receive. Unlike `SBBroadcaster.AddListener` this also works for broadcasters
that don\'t exist yet, so it can be used to catch the events of a process before
it is created::

    listener.StartListeningForEventClass(debugger,
                                         lldb.SBTarget.GetBroadcasterClassName(),
                                         lldb.SBTarget.eBroadcastBitModulesLoaded)

Returns the event bits that were successfully subscribed to."
) lldb::SBListener::StartListeningForEventClass;

%feature("docstring",
"Unsubscribes this listener from a class of broadcasters.

See `SBListener.StartListeningForEventClass`."
) lldb::SBListener::StopListeningForEventClass;

%feature("docstring",
"Subscribes this listener to the events of one broadcaster.

``event_mask`` selects which events of the broadcaster are delivered. Returns the
event bits that were successfully subscribed to. This is the same as
`SBBroadcaster.AddListener`."
) lldb::SBListener::StartListeningForEvents;

%feature("docstring",
"Unsubscribes this listener from the events of one broadcaster.

See `SBListener.StartListeningForEvents`."
) lldb::SBListener::StopListeningForEvents;

%feature("docstring",
"Waits for the next event and fills it into the given `SBEvent`.

Waits at most ``num_seconds`` seconds; pass ``lldb.UINT32_MAX`` to wait forever.
Returns ``False`` if the wait timed out::

    event = lldb.SBEvent()
    if listener.WaitForEvent(5, event):
        print(lldb.SBDebugger.StateAsCString(lldb.SBProcess.GetStateFromEvent(event)))

The event is removed from the listener\'s queue; use
`SBListener.PeekAtNextEvent` to look at it without consuming it."
) lldb::SBListener::WaitForEvent;

%feature("docstring",
"Waits for the next event of a specific broadcaster.

Behaves like `SBListener.WaitForEvent`, but events from other broadcasters are
ignored."
) lldb::SBListener::WaitForEventForBroadcaster;

%feature("docstring",
"Waits for the next event of a specific broadcaster and type.

Behaves like `SBListener.WaitForEventForBroadcaster`, but only events whose
type matches ``event_type_mask`` are returned."
) lldb::SBListener::WaitForEventForBroadcasterWithType;

%feature("docstring",
"Fills in the next event without removing it from the queue.

Returns ``False`` if there is no event pending. Unlike
`SBListener.WaitForEvent` this returns immediately."
) lldb::SBListener::PeekAtNextEvent;

%feature("docstring",
"Fills in the next event of a specific broadcaster without removing it.

See `SBListener.PeekAtNextEvent`."
) lldb::SBListener::PeekAtNextEventForBroadcaster;

%feature("docstring",
"Fills in the next event of a specific broadcaster and type without removing it.

See `SBListener.PeekAtNextEvent`."
) lldb::SBListener::PeekAtNextEventForBroadcasterWithType;

%feature("docstring",
"Fills in the next event and removes it from the queue.

Returns ``False`` if there is no event pending. Unlike
`SBListener.WaitForEvent` this returns immediately instead of waiting."
) lldb::SBListener::GetNextEvent;

%feature("docstring",
"Fills in the next event of a specific broadcaster and removes it from the queue.

See `SBListener.GetNextEvent`."
) lldb::SBListener::GetNextEventForBroadcaster;

%feature("docstring",
"Fills in the next event of a specific broadcaster and type and removes it.

See `SBListener.GetNextEvent`."
) lldb::SBListener::GetNextEventForBroadcasterWithType;

%feature("docstring",
"Handles an event that was broadcast to this listener.

This is used by the internal event handling of LLDB; scripts usually wait for
events instead, see `SBListener.WaitForEvent`."
) lldb::SBListener::HandleBroadcastEvent;
