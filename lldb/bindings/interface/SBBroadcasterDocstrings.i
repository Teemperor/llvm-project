%feature("docstring",
"Represents an entity which can broadcast events.

A default broadcaster is
associated with an SBCommandInterpreter, SBProcess, and SBTarget.  For
example, use ::

    broadcaster = process.GetBroadcaster()

to retrieve the process\'s broadcaster.

Every broadcaster defines a set of event bits, which are the ``eBroadcastBit*``
constants of the class it belongs to (for example
``lldb.SBProcess.eBroadcastBitStateChanged``). An `SBListener` subscribes to some
of those bits and then receives the matching events::

    listener = lldb.SBListener('my listener')
    process.GetBroadcaster().AddListener(listener,
                                         lldb.SBProcess.eBroadcastBitStateChanged |
                                         lldb.SBProcess.eBroadcastBitSTDOUT)

See also SBEvent for example usage of interacting with a broadcaster."
) lldb::SBBroadcaster;

%feature("docstring",
"Returns whether this object refers to a broadcaster."
) lldb::SBBroadcaster::IsValid;

%feature("docstring",
"Resets this object to an invalid broadcaster."
) lldb::SBBroadcaster::Clear;

%feature("docstring",
"Sends an event that only carries the given type to the listeners.

If ``unique`` is ``True`` and an event of that type is already queued for a
listener, no new event is sent to it."
) lldb::SBBroadcaster::BroadcastEventByType;

%feature("docstring",
"Sends the given `SBEvent` to the listeners of this broadcaster.

If ``unique`` is ``True`` and an event of the same type is already queued for a
listener, no new event is sent to it."
) lldb::SBBroadcaster::BroadcastEvent;

%feature("docstring",
"Sends the events that describe the current state to a listener.

A listener that subscribes to a broadcaster after interesting things already
happened can use this to catch up: the broadcaster sends the events that
communicate its current state."
) lldb::SBBroadcaster::AddInitialEventsToListener;

%feature("docstring",
"Subscribes the given `SBListener` to the events in ``event_mask``.

``event_mask`` is a bit mask of this broadcaster\'s event bits. Returns the bits
that were successfully subscribed to::

    process.GetBroadcaster().AddListener(listener,
                                         lldb.SBProcess.eBroadcastBitStateChanged)
"
) lldb::SBBroadcaster::AddListener;

%feature("docstring",
"Returns the name of this broadcaster."
) lldb::SBBroadcaster::GetName;

%feature("docstring",
"Returns whether any listener is subscribed to the given event type."
) lldb::SBBroadcaster::EventTypeHasListeners;

%feature("docstring",
"Unsubscribes the given listener from the events in ``event_mask``.

Returns whether the listener was subscribed to any of those events."
) lldb::SBBroadcaster::RemoveListener;
