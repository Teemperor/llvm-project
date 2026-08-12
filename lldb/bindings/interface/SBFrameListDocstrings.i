%feature("docstring",
"Represents the stack frames of a thread.

Frame lists are returned by `SBThread.GetFrames`, which fetches all frames of a
thread at once. That is faster than calling `SBThread.GetFrameAtIndex` in a loop,
because the frames are computed as a batch.

In Python the list supports ``len()``, indexing and iteration::

    for frame in thread.GetFrames():
        print(frame.GetDisplayFunctionName())

See also :py:class:`SBFrame` and :py:class:`SBThread`."
) lldb::SBFrameList;

%feature("docstring",
"Returns whether this object holds a list of frames."
) lldb::SBFrameList::IsValid;

%feature("docstring",
"Returns the number of frames in this list.

In Python this is also what ``len()`` returns."
) lldb::SBFrameList::GetSize;

%feature("docstring",
"Returns the frame at the given index as an `SBFrame`.

Frame ``0`` is the innermost (currently executing) frame."
) lldb::SBFrameList::GetFrameAtIndex;

%feature("docstring",
"Returns the `SBThread` these frames belong to."
) lldb::SBFrameList::GetThread;

%feature("docstring",
"Removes all frames from this list."
) lldb::SBFrameList::Clear;

%feature("docstring",
"Writes a description of all frames in this list into the given `SBStream`."
) lldb::SBFrameList::GetDescription;
