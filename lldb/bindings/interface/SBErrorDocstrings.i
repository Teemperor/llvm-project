%feature("docstring",
"Reports whether an operation succeeded and why it failed.

Most API functions that can fail take an SBError as an output parameter or return
one. Always check `SBError.Success` (or `SBError.Fail`) before using the result of
such a call, and use `SBError.GetCString` to get the message::

    error = lldb.SBError()
    process = target.Launch(launch_info, error)
    if error.Fail():
        print('launch failed: %s' % error.GetCString())

An error that was never set is in the success state, so a default constructed
SBError can be passed to any function that expects one. Values also carry an
error that explains why they could not be read, see `SBValue.GetError`.

For example (from test/python_api/hello_world/TestHelloWorld.py), ::

    def hello_world_attach_with_id_api(self):
        '''Create target, spawn a process, and attach to it by id.'''

        target = self.dbg.CreateTarget(self.exe)

        # Spawn a new process and don't display the stdout if not in TraceOn() mode.
        import subprocess
        popen = subprocess.Popen(
            [self.exe, 'abc', 'xyz'],
            stdout=subprocess.DEVNULL if not self.TraceOn() else None,
        )

        listener = lldb.SBListener('my.attach.listener')
        error = lldb.SBError()
        process = target.AttachToProcessWithID(listener, popen.pid, error)

        self.assertTrue(error.Success() and process, PROCESS_IS_VALID)

        # Let's check the stack traces of the attached process.
        import lldbutil
        stacktraces = lldbutil.print_stacktraces(process, string_buffer=True)
        self.expect(stacktraces, exe=False,
            substrs = ['main.c:%d' % self.line2,
                       '(int)argc=3'])

        listener = lldb.SBListener('my.attach.listener')
        error = lldb.SBError()
        process = target.AttachToProcessWithID(listener, popen.pid, error)

        self.assertTrue(error.Success() and process, PROCESS_IS_VALID)

checks that after the attach, there is no error condition by asserting
that error.Success() is True and we get back a valid process object.

And (from test/python_api/event/TestEvent.py), ::

        # Now launch the process, and do not stop at entry point.
        error = lldb.SBError()
        process = target.Launch(listener, None, None, None, None, None, None, 0, False, error)
        self.assertTrue(error.Success() and process, PROCESS_IS_VALID)

checks that after calling the target.Launch() method there's no error
condition and we get back a void process object.") lldb::SBError;

%feature("docstring",
"Returns whether this object holds an error code.

Note that a *valid* error can still describe a success; use
`SBError.Success` to check whether an operation worked."
) lldb::SBError::IsValid;

%feature("docstring",
"Returns the error message as a string.

Returns ``None`` if this error describes a success."
) lldb::SBError::GetCString;

%feature("docstring",
"Resets this object to the success state."
) lldb::SBError::Clear;

%feature("docstring",
"Returns whether an error occurred.

This is the opposite of `SBError.Success`."
) lldb::SBError::Fail;

%feature("docstring",
"Returns whether the operation this error belongs to succeeded.

An error object that was never filled in is in the success state."
) lldb::SBError::Success;

%feature("docstring",
"Returns the error code as an integer.

What the number means depends on `SBError.GetType`, for example it is an
``errno`` value for ``lldb.eErrorTypePOSIX`` errors."
) lldb::SBError::GetError;

%feature("docstring",
"Returns the error as `SBStructuredData`.

Structured errors carry machine readable details in addition to the message,
which is used for example by the DAP implementation to report rich errors."
) lldb::SBError::GetErrorData;

%feature("docstring",
"Returns what kind of error this is as one of the ``lldb.eErrorType*``
enumerators.

Common values are ``lldb.eErrorTypePOSIX`` for ``errno`` values,
``lldb.eErrorTypeGeneric`` and ``lldb.eErrorTypeInvalid`` for an error that was
not set."
) lldb::SBError::GetType;

%feature("docstring",
"Sets the error code and its type.

``type`` is one of the ``lldb.eErrorType*`` enumerators. Mostly useful when
implementing a scripted plugin that has to report failures back to LLDB."
) lldb::SBError::SetError;

%feature("docstring",
"Sets this error from the current value of ``errno``."
) lldb::SBError::SetErrorToErrno;

%feature("docstring",
"Sets this error to a generic error without a specific code."
) lldb::SBError::SetErrorToGenericError;

%feature("docstring",
"Sets the error message of this error.

Use this in scripted plugins to report a failure::

    def my_callback():
        return lldb.SBError('something went wrong')
"
) lldb::SBError::SetErrorString;

%feature("docstring",
"Sets the error message from a format string and its arguments."
) lldb::SBError::SetErrorStringWithFormat;

%feature("docstring",
"Writes a description of this error into the given `SBStream`."
) lldb::SBError::GetDescription;
