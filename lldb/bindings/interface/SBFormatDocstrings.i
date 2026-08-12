%feature("docstring",
"Class that represents a format string that can be used to generate
descriptions of objects like frames and threads.

A format is created from a format string as described in
https://lldb.llvm.org/use/formatting.html, and it is then used by
`SBFrame.GetDescriptionWithFormat` and
`SBThread.GetDescriptionWithFormat` to render an object the same way the
command line interface would::

    error = lldb.SBError()
    format = lldb.SBFormat('frame #${frame.index}: ${function.name}', error)
    if error.Fail():
        print(error.GetCString())

    stream = lldb.SBStream()
    frame.GetDescriptionWithFormat(format, stream)
    print(stream.GetData())

If the format string cannot be parsed, the resulting object is invalid and the
`SBError` that was passed to the constructor explains why."
) lldb::SBFormat;
