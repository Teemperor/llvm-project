%feature("docstring",
"Represents a file specification that divides the path into a directory and
basename.  The string values of the paths are put into uniqued string pools
for fast comparisons and efficient memory usage.

For example, the following code ::

        lineEntry = context.GetLineEntry()
        self.expect(lineEntry.GetFileSpec().GetDirectory(), 'The line entry should have the correct directory',
                    exe=False,
            substrs = [self.mydir])
        self.expect(lineEntry.GetFileSpec().GetFilename(), 'The line entry should have the correct filename',
                    exe=False,
            substrs = ['main.c'])
        self.assertTrue(lineEntry.GetLine() == self.line,
                        'The line entry's line number should match ')

gets the line entry from the symbol context when a thread is stopped.
It gets the file spec corresponding to the line entry and checks that
the filename and the directory matches what we expect.

File specs are used wherever the API refers to a file: the source file of a line
entry (`SBLineEntry.GetFileSpec`), the executable of a module
(`SBModule.GetFileSpec`), the file a breakpoint is set in
(`SBTarget.BreakpointCreateByLocation`) and the files that are transferred to a
platform (`SBPlatform.Put`). They can be created from a path::

    file = lldb.SBFileSpec('/path/to/main.c')
    print(file.GetDirectory(), file.GetFilename())

    # A file spec that only has a file name matches any directory, which is what
    # the breakpoint functions expect.
    breakpoint = target.BreakpointCreateByLocation(lldb.SBFileSpec('main.c'), 42)

Note that a file spec does not have to refer to a file that exists, see
`SBFileSpec.Exists`.") lldb::SBFileSpec;

%feature("docstring",
"Returns whether this object holds a file specification."
) lldb::SBFileSpec::IsValid;

%feature("docstring",
"Returns whether the file this specification refers to exists.

Only checks the local file system; for a file on a remote platform use
`SBPlatform.GetFilePermissions` instead."
) lldb::SBFileSpec::Exists;

%feature("docstring",
"Looks the file up in the directories of the ``PATH`` environment variable.

If the file is found, this object is updated to its full path. Returns whether
the file was found."
) lldb::SBFileSpec::ResolveExecutableLocation;

%feature("docstring",
"Returns the file name of this specification without its directory."
) lldb::SBFileSpec::GetFilename;

%feature("docstring",
"Returns the directory of this specification without the file name.

Returns ``None`` for a specification that only consists of a file name."
) lldb::SBFileSpec::GetDirectory;

%feature("docstring",
"Sets the file name of this specification, see
`SBFileSpec.GetFilename`."
) lldb::SBFileSpec::SetFilename;

%feature("docstring",
"Sets the directory of this specification, see
`SBFileSpec.GetDirectory`."
) lldb::SBFileSpec::SetDirectory;

%feature("docstring",
"Returns the full path of this specification.

In Python this takes no arguments and returns the path as a string::

    print(module.GetFileSpec().GetPath())
"
) lldb::SBFileSpec::GetPath;

%feature("docstring",
"Resolves ``~`` and relative components of a path.

This is a class method that takes the path to resolve and, in C++, the buffer to
write the result into."
) lldb::SBFileSpec::ResolvePath;

%feature("docstring",
"Writes a description of this file specification into the given `SBStream`."
) lldb::SBFileSpec::GetDescription;

%feature("docstring",
"Appends a path component to this specification.

Turns the current path into a directory and appends the given name to it, so
appending ``main.c`` to ``/tmp`` results in ``/tmp/main.c``."
) lldb::SBFileSpec::AppendPathComponent;
