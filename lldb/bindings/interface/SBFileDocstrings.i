%feature("docstring",
"A file that LLDB can read from or write to.

Files are how the input and the output of a debugger are redirected
(`SBDebugger.SetOutputFile`, `SBDebugger.GetInputFile`) and where some functions
write their output.

In Python an SBFile can be created from any file-like object, either with the
constructor or with the ``SBFile.Create(file, borrow=False,
force_io_methods=False)`` class method, which also decides whether the file is
borrowed (LLDB does not close it when the SBFile goes away)::

    import io
    output = io.StringIO()
    debugger.SetOutputFile(lldb.SBFile.Create(output, borrow=True))
    debugger.HandleCommand('breakpoint list')
    print(output.getvalue())

It can also be created from a file descriptor together with a mode string, e.g.
``lldb.SBFile(1, 'w', False)`` for the standard output."
) lldb::SBFile;

%feature("docstring", "
Initialize a SBFile from a file descriptor.  mode is
'r', 'r+', or 'w', like fdopen.") lldb::SBFile::SBFile;

%feature("docstring", "initialize a SBFile from a python file object") lldb::SBFile::SBFile;

%feature("docstring",
"Returns whether this object refers to a file."
) lldb::SBFile::IsValid;

%feature("autodoc", "Read(buffer) -> SBError, bytes_read") lldb::SBFile::Read;

%feature("docstring",
"Reads up to ``num_bytes`` bytes from this file.

In Python this returns a tuple of an `SBError` and the bytes that were read."
) lldb::SBFile::Read;

%feature("autodoc", "Write(buffer) -> SBError, written_read") lldb::SBFile::Write;

%feature("docstring",
"Writes the given bytes to this file.

In Python this returns a tuple of an `SBError` and the number of bytes that were
written."
) lldb::SBFile::Write;

%feature("docstring",
"Writes any buffered data of this file out and returns an `SBError`."
) lldb::SBFile::Flush;

%feature("docstring",
"Closes this file and returns an `SBError`.

Whether the underlying file object is really closed depends on whether this
`SBFile` owns it, see `SBFile.Create`."
) lldb::SBFile::Close;

%feature("docstring", "
    Convert this SBFile into a python io.IOBase file object.

    If the SBFile is itself a wrapper around a python file object,
    this will return that original object.

    The file returned from here should be considered borrowed,
    in the sense that you may read and write to it, and flush it,
    etc, but you should not close it.   If you want to close the
    SBFile, call `SBFile.Close`.

    If there is no underlying python file to unwrap, GetFile will
    use the file descriptor, if available to create a new python
    file object using ``open(fd, mode=..., closefd=False)``
") lldb::SBFile::GetFile;
