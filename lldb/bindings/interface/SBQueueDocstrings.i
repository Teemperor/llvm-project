%feature("docstring",
"Represents a libdispatch queue in the process.

Queues exist on systems that use libdispatch (Grand Central Dispatch). A queue
holds the work items that were submitted to it and the threads that currently
run them, which makes it possible to see what a program is doing even when the
work is spread across a thread pool.

Queues are obtained from a process (`SBProcess.GetQueueAtIndex`) or from a thread
that currently runs a work item of the queue (`SBThread.GetQueue`)::

    for i in range(process.GetNumQueues()):
        queue = process.GetQueueAtIndex(i)
        print('%s: %d running, %d pending' % (queue.GetName(),
                                             queue.GetNumRunningItems(),
                                             queue.GetNumPendingItems()))

See also :py:class:`SBQueueItem` and :py:class:`SBThread`."
) lldb::SBQueue;

%feature("docstring",
"Returns whether this object refers to a queue."
) lldb::SBQueue::IsValid;

%feature("docstring",
"Resets this object to an invalid queue."
) lldb::SBQueue::Clear;

%feature("docstring",
"Returns the `SBProcess` this queue belongs to."
) lldb::SBQueue::GetProcess;

%feature("docstring", "
    Returns an lldb::queue_id_t type unique identifier number for this
    queue that will not be used by any other queue during this process\'
    execution.  These ID numbers often start at 1 with the first
    system-created queues and increment from there."
) lldb::SBQueue::GetQueueID;

%feature("docstring",
"Returns the name of this queue, e.g. ``com.apple.main-thread``."
) lldb::SBQueue::GetName;

%feature("docstring",
"Returns the index ID of this queue.

Unlike `SBQueue.GetQueueID` this is a small number that LLDB assigns and that is
shown to users."
) lldb::SBQueue::GetIndexID;

%feature("docstring",
"Returns the number of threads that currently run work items of this queue.

See `SBQueue.GetThreadAtIndex`."
) lldb::SBQueue::GetNumThreads;

%feature("docstring",
"Returns the thread at the given index as an `SBThread`.

These are the threads that currently execute work items of this queue."
) lldb::SBQueue::GetThreadAtIndex;

%feature("docstring",
"Returns the number of work items that were submitted but haven\'t started yet.

See `SBQueue.GetPendingItemAtIndex`."
) lldb::SBQueue::GetNumPendingItems;

%feature("docstring",
"Returns the pending work item at the given index as an `SBQueueItem`.

Pending items are the ones that were enqueued but are not running yet, so their
address tells where the work will start::

    item = queue.GetPendingItemAtIndex(0)
    print(item.GetAddress().GetSymbol().GetName())
"
) lldb::SBQueue::GetPendingItemAtIndex;

%feature("docstring",
"Returns the number of work items of this queue that are currently running."
) lldb::SBQueue::GetNumRunningItems;

%feature("docstring", "
    Returns an lldb::QueueKind enumerated value (e.g. eQueueKindUnknown,
    eQueueKindSerial, eQueueKindConcurrent) describing the type of this
    queue."
) lldb::SBQueue::GetKind;
