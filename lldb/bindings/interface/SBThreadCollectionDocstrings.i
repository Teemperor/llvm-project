%feature("docstring",
"Represents a collection of SBThread objects.

Thread collections are returned by the functions that produce history threads:
`SBProcess.GetHistoryThreads` and
`SBThread.GetStopReasonExtendedBacktraces`. History threads are not real
threads of the process; they describe where something happened in the past, for
example where a block of memory was allocated and freed, and they cannot be
resumed.

In Python the collection supports ``len()``, indexing and iteration::

    for thread in process.GetHistoryThreads(addr):
        for frame in thread:
            print(frame)
"
) lldb::SBThreadCollection;

%feature("docstring",
"Returns whether this object holds a collection of threads."
) lldb::SBThreadCollection::IsValid;

%feature("docstring",
"Returns the number of threads in this collection.

In Python this is also what ``len()`` returns."
) lldb::SBThreadCollection::GetSize;

%feature("docstring",
"Returns the thread at the given index as an `SBThread`."
) lldb::SBThreadCollection::GetThreadAtIndex;
