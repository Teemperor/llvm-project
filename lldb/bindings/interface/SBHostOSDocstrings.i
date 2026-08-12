%feature("docstring",
"Provides information about the host system.

All functions of this class are static. They are mostly useful to find the
directories LLDB itself was installed in, for example to locate the Python
modules that ship with it::

    print(lldb.SBHostOS.GetLLDBPath(lldb.ePathTypePythonDir).GetDirectory())
"
) lldb::SBHostOS;

%feature("docstring",
"Returns the executable of the current process as an `SBFileSpec`.

For a script that runs inside LLDB this is the ``lldb`` binary itself, for a
program that links against LLDB it is that program."
) lldb::SBHostOS::GetProgramFileSpec;

%feature("docstring",
"Deprecated, use `SBHostOS.GetScriptPath`."
) lldb::SBHostOS::GetLLDBPythonPath;

%feature("docstring",
"Returns the directory that holds LLDB\'s scripting support files.

``language`` is one of the ``lldb.eScriptLanguage*`` enumerators; for Python this
is the directory that contains the ``lldb`` Python module."
) lldb::SBHostOS::GetScriptPath;

%feature("docstring",
"Returns one of the paths of the LLDB installation as an `SBFileSpec`.

``path_type`` is one of the ``lldb.ePathType*`` enumerators, which selects for
example the directory of the LLDB shared library, its header files, its Python
modules or the directory for temporary files."
) lldb::SBHostOS::GetLLDBPath;

%feature("docstring",
"Returns the home directory of the current user as an `SBFileSpec`."
) lldb::SBHostOS::GetUserHomeDirectory;

%feature("docstring",
"Deprecated, the threading functions of this class are not well supported."
) lldb::SBHostOS::ThreadCreated;

%feature("docstring",
"Deprecated, the threading functions of this class are not well supported."
) lldb::SBHostOS::ThreadCreate;

%feature("docstring",
"Deprecated, the threading functions of this class are not well supported."
) lldb::SBHostOS::ThreadCancel;

%feature("docstring",
"Deprecated, the threading functions of this class are not well supported."
) lldb::SBHostOS::ThreadDetach;

%feature("docstring",
"Deprecated, the threading functions of this class are not well supported."
) lldb::SBHostOS::ThreadJoin;
