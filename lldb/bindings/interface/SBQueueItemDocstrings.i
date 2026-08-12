%feature("docstring",
"This class represents an item in an :py:class:`SBQueue`.

A queue item is one piece of work that was submitted to a libdispatch queue. Its
address (`SBQueueItem.GetAddress`) is the function that will be run, which is
what makes it possible to see what work is still pending::

    for i in range(queue.GetNumPendingItems()):
        item = queue.GetPendingItemAtIndex(i)
        print(item.GetAddress().GetSymbol().GetName())

Items are obtained from `SBQueue.GetPendingItemAtIndex`."
) lldb::SBQueueItem;

%feature("docstring",
"Returns whether this object refers to a queue item."
) lldb::SBQueueItem::IsValid;

%feature("docstring",
"Resets this object to an invalid queue item."
) lldb::SBQueueItem::Clear;

%feature("docstring",
"Returns what kind of work item this is.

The result is one of the ``lldb.eQueueItemKind*`` enumerators, which
distinguishes for example a function that was enqueued from a block."
) lldb::SBQueueItem::GetKind;

%feature("docstring",
"Sets the kind of this work item.

Used by the plugins that provide queue information; see
`SBQueueItem.GetKind`."
) lldb::SBQueueItem::SetKind;

%feature("docstring",
"Returns the address of the function this work item will run as an `SBAddress`.

Resolve it to a symbol to find out what the work is, see
`SBAddress.GetSymbol`."
) lldb::SBQueueItem::GetAddress;

%feature("docstring",
"Sets the address of the function this work item will run.

Used by the plugins that provide queue information."
) lldb::SBQueueItem::SetAddress;

%feature("docstring",
"Returns a history thread describing where this work item was enqueued.

``type`` is the name of one of the extended backtrace types the process supports,
see `SBProcess.GetExtendedBacktraceTypeAtIndex`. The returned `SBThread` is a
history thread: it can be used for its backtrace but it cannot be resumed."
) lldb::SBQueueItem::GetExtendedBacktraceThread;
