%feature("docstring",
"Controls LLDB\'s reproducer capture.

A reproducer records the API calls and the input a debug session received so it
can be replayed later, which is used to investigate LLDB bugs. All functions of
this class are static and are only useful to LLDB\'s own tooling; see
`SBDebugger.GetReproducerPath` for the location of a capture that is in
progress."
) lldb::SBReproducer;
