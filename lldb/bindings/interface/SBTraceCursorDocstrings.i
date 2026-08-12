%feature("docstring",
"Iterates over the items of a processor trace.

A trace cursor points at one item of a thread\'s trace, which is either an
executed instruction, an error or an event, and it can be moved through the trace
with `SBTraceCursor.Next` and `SBTraceCursor.Seek`. Cursors are created with
`SBTrace.CreateNewCursor`.

A new cursor points at the *end* of the trace and moves backwards, which is
usually what a debugger wants: the most recent instructions are the interesting
ones. Use `SBTraceCursor.SetForwards` to walk the trace chronologically
instead::

    error = lldb.SBError()
    cursor = trace.CreateNewCursor(error, thread)
    cursor.SetForwards(True)
    while cursor.HasValue():
        if cursor.IsError():
            print('error: %s' % cursor.GetError())
        elif cursor.IsEvent():
            print('event: %s' % cursor.GetEventTypeAsString())
        elif cursor.IsInstruction():
            print(hex(cursor.GetLoadAddress()))
        cursor.Next()

Every item has an identifier (`SBTraceCursor.GetId`) that is unique within the
thread\'s trace. The identifiers are not necessarily 0-based indices: each trace
plug-in decides how it encodes them, so no assumptions about their order or
sequentiality can be made. They exist so that a tool can quickly return to a
place in the trace it saw before (`SBTraceCursor.GoToId`), which is much cheaper
than walking there. Moving to the next item with `SBTraceCursor.Next` is still
the fastest way to iterate, so ``GoToId`` should be used sparingly."
) lldb::SBTraceCursor;

%feature("docstring",
"Returns whether this object refers to a trace cursor."
) lldb::SBTraceCursor::IsValid;

%feature("docstring",
"Set the direction to use in the `SBTraceCursor.Next` method.

:param forwards: If ``True``, then the traversal will be forwards, otherwise
    backwards."
) lldb::SBTraceCursor::SetForwards;

%feature("docstring",
"Check if the direction to use in the `SBTraceCursor.Next` method is forwards.

:return: ``True`` if the current direction is forwards, ``False`` if backwards."
) lldb::SBTraceCursor::IsForwards;

%feature("docstring",
"Move the cursor to the next item (instruction, error or event).

The traversal is done following the current direction of the trace. If
it is forwards, the instructions are visited forwards chronologically.
Otherwise, the traversal is done in the opposite direction. By default, a cursor
moves backwards unless changed with `SBTraceCursor.SetForwards`."
) lldb::SBTraceCursor::Next;

%feature("docstring",
"Returns whether the cursor points to a valid item.

Returns ``False`` once the cursor moved past the end (or the beginning) of the
trace, which is how iteration terminates."
) lldb::SBTraceCursor::HasValue;

%feature("docstring",
"Make the cursor point to the item whose identifier is ``id``.

:return: ``True`` if the given identifier exists and the cursor effectively moved
    to it. Otherwise, ``False`` is returned and the cursor now points to an
    invalid item, i.e. calling `SBTraceCursor.HasValue` will return ``False``."
) lldb::SBTraceCursor::GoToId;

%feature("docstring",
"Returns whether there is an item with the given identifier in this trace."
) lldb::SBTraceCursor::HasId;

%feature("docstring",
"Returns the unique identifier of the item this cursor is pointing to.

It can be passed to `SBTraceCursor.GoToId` to come back to this item later."
) lldb::SBTraceCursor::GetId;

%feature("docstring",
"Move the cursor to an item in the trace based on an origin point and an offset.

The resulting position is ``origin + offset``. If that would be out of bounds,
the cursor points to an invalid item, i.e. `SBTraceCursor.HasValue` returns
``False``.

:param offset: How many items to move forwards (if positive) or backwards (if
    negative) from the given origin point.
:param origin: One of the ``lldb.eTraceCursorSeek*`` enumerators
    (``Beginning``, ``Current`` or ``End``) that says where to start counting
    from.
:return: ``True`` if and only if the cursor ends up pointing to a valid item."
) lldb::SBTraceCursor::Seek;

%feature("docstring",
"Returns the kind of item the cursor is pointing at.

The result is one of the ``lldb.eTraceItemKind*`` enumerators; the
`SBTraceCursor.IsInstruction`, `SBTraceCursor.IsError` and
`SBTraceCursor.IsEvent` functions are shortcuts for the common checks."
) lldb::SBTraceCursor::GetItemKind;

%feature("docstring",
"Returns whether the cursor points to an error.

Errors appear in a trace where the tracing technology could not record what
happened, for example because the trace buffer overflowed."
) lldb::SBTraceCursor::IsError;

%feature("docstring",
"Returns the error message the cursor is pointing at."
) lldb::SBTraceCursor::GetError;

%feature("docstring",
"Returns whether the cursor points to an event.

Events are things that happened during the trace that are not instructions, such
as the thread being scheduled out or the trace being paused."
) lldb::SBTraceCursor::IsEvent;

%feature("docstring",
"Returns the specific kind of event the cursor is pointing at.

The result is one of the ``lldb.eTraceEvent*`` enumerators, see
`SBTraceCursor.GetEventTypeAsString` for a human readable version."
) lldb::SBTraceCursor::GetEventType;

%feature("docstring",
"Returns a human-readable description of the event this cursor is pointing at."
) lldb::SBTraceCursor::GetEventTypeAsString;

%feature("docstring",
"Returns whether the cursor points to an instruction."
) lldb::SBTraceCursor::IsInstruction;

%feature("docstring",
"Returns the load address of the instruction the cursor is pointing at.

Pass it to `SBTarget.ResolveLoadAddress` to find out which function the
instruction belongs to."
) lldb::SBTraceCursor::GetLoadAddress;

%feature("docstring",
"Returns the CPU the current item was recorded on.

Returns ``lldb.LLDB_INVALID_CPU_ID`` if the trace does not provide this
information."
) lldb::SBTraceCursor::GetCPU;
