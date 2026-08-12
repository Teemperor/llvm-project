%feature("docstring",
"Represents a central authority for displaying source code.

The source manager reads the source files of a debug session and caches them.
Every `SBDebugger` and `SBTarget` has one (`SBDebugger.GetSourceManager`,
`SBTarget.GetSourceManager`); the target\'s knows about the target\'s path
mappings, so it should be preferred when a target is available.

For example (from test/source-manager/TestSourceManager.py), ::

        # Create the filespec for \'main.c\'.
        filespec = lldb.SBFileSpec(\'main.c\', False)
        source_mgr = self.dbg.GetSourceManager()
        # Use a string stream as the destination.
        stream = lldb.SBStream()
        source_mgr.DisplaySourceLinesWithLineNumbers(filespec,
                                                     self.line,
                                                     2, # context before
                                                     2, # context after
                                                     \'=>\', # prefix for current line
                                                     stream)

        #    2
        #    3    int main(int argc, char const *argv[]) {
        # => 4        printf(\'Hello world.\\n\'); // Set break point at this line.
        #    5        return 0;
        #    6    }
        self.expect(stream.GetData(), \'Source code displayed correctly\',
                    exe=False,
            patterns = [\'=> %d.*Hello world\' % self.line])"
) lldb::SBSourceManager;

%feature("docstring",
"Writes source lines around the given line into an `SBStream`.

``file`` is the `SBFileSpec` of the source file, ``line`` the line to show,
``context_before`` and ``context_after`` how many lines around it to include, and
``current_line_cstr`` a prefix that marks the current line. Returns the number of
lines that were written."
) lldb::SBSourceManager::DisplaySourceLinesWithLineNumbers;

%feature("docstring",
"Writes source lines around the given line and column into an `SBStream`.

Behaves like
`SBSourceManager.DisplaySourceLinesWithLineNumbers`, but also marks the given
column of the current line."
) lldb::SBSourceManager::DisplaySourceLinesWithLineNumbersAndColumn;
