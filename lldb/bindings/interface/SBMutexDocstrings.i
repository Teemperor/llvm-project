%feature("docstring",
"A lock that guards the API of a target.

`SBTarget.GetAPIMutex` returns the mutex of a target. Locking it around a group of
API calls keeps another thread from changing the target in between them, which
matters for scripts that use LLDB from several threads or that run while the
process is being resumed by someone else.

In Python an SBMutex is a context manager, so the recommended way to use it is a
``with`` statement::

    with target.GetAPIMutex():
        thread = process.GetSelectedThread()
        frame = thread.GetFrameAtIndex(0)

The `SBMutex.lock` and `SBMutex.unlock` methods are also available for cases
where the scope of the lock cannot be expressed with ``with``. Note that the lock
is recursive, so locking it from a thread that already holds it is allowed."
) lldb::SBMutex;

%feature("docstring",
"Returns whether this object refers to a mutex."
) lldb::SBMutex::IsValid;

%feature("docstring",
"Acquires the lock, waiting until it is available.

Prefer using the mutex as a context manager, which unlocks it even if an
exception is raised. Every call has to be paired with a call to
`SBMutex.unlock`."
) lldb::SBMutex::lock;

%feature("docstring",
"Releases the lock, see `SBMutex.lock`."
) lldb::SBMutex::unlock;

%feature("docstring",
"Tries to acquire the lock without waiting.

Returns whether the lock was acquired. If it was, it has to be released with
`SBMutex.unlock`."
) lldb::SBMutex::try_lock;
