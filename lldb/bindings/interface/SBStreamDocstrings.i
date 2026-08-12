%feature("docstring",
"Collects text that the API writes, by default in memory.

Many API functions write their output into an SBStream instead of returning a
string, for example the ``GetDescription`` functions of most classes and
`SBSourceManager.DisplaySourceLinesWithLineNumbers`. Create one, pass it in and
read the text back with `SBStream.GetData`::

    stream = lldb.SBStream()
    frame.GetDescription(stream)
    print(stream.GetData())

A stream can also be redirected to a file with
`SBStream.RedirectToFile`, in which case the text is written there instead of
being kept in memory.

For example (from test/source-manager/TestSourceManager.py), ::

        # Create the filespec for 'main.c'.
        filespec = lldb.SBFileSpec('main.c', False)
        source_mgr = self.dbg.GetSourceManager()
        # Use a string stream as the destination.
        stream = lldb.SBStream()
        source_mgr.DisplaySourceLinesWithLineNumbers(filespec,
                                                     self.line,
                                                     2, # context before
                                                     2, # context after
                                                     '=>', # prefix for current line
                                                     stream)

        #    2
        #    3    int main(int argc, char const *argv[]) {
        # => 4        printf('Hello world.\\n'); // Set break point at this line.
        #    5        return 0;
        #    6    }
        self.expect(stream.GetData(), 'Source code displayed correctly',
                    exe=False,
            patterns = ['=> %d.*Hello world' % self.line])"
) lldb::SBStream;

%feature("docstring", "
    If this stream is not redirected to a file, it will maintain a local
    cache for the stream data which can be accessed using this accessor."
) lldb::SBStream::GetData;

%feature("docstring", "
    If this stream is not redirected to a file, it will maintain a local
    cache for the stream output whose length can be accessed using this
    accessor."
) lldb::SBStream::GetSize;

%feature("docstring", "
    If the stream is redirected to a file, forget about the file and if
    ownership of the file was transferred to this object, close the file.
    If the stream is backed by a local cache, clear this cache."
) lldb::SBStream::Clear;

%feature("docstring", "
    Returns whether this stream is ready to be written to."
) lldb::SBStream::IsValid;

%feature("docstring", "
    Writes the given string to this stream."
) lldb::SBStream::Print;

%feature("docstring", "
    Redirects this stream to the file at the given path.

    If ``append`` is ``True`` the text is added to the end of an existing file,
    otherwise the file is truncated. The data that was collected in memory so far
    is written to the file as well. See `SBStream.Clear` to stop redirecting."
) lldb::SBStream::RedirectToFile;

%feature("docstring", "
    Redirects this stream to an already opened file.

    In Python a file object can be passed directly. If ``transfer_fh_ownership``
    is ``True`` the stream closes the file when it is done with it."
) lldb::SBStream::RedirectToFileHandle;

%feature("docstring", "
    Redirects this stream to the given file descriptor.

    If ``transfer_fh_ownership`` is ``True`` the stream closes the descriptor when
    it is done with it."
) lldb::SBStream::RedirectToFileDescriptor;
