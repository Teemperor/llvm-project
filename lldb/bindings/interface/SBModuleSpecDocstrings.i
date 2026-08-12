%feature("docstring",
"Describes a module so it can be found, added to a target or matched.

A module spec is a set of criteria a module has to satisfy: a file path, an
architecture triple, a UUID, and for files that contain several objects (such as
static archives or universal Mach-O files) the name, offset and size of the
object inside the file. Not all of them have to be set; the fields that are set
are the ones that have to match.

Module specs are used to add modules to a target
(`SBTarget.AddModule`), to look modules up (`SBTarget.FindModule`) and to
inspect what an object file contains
(`SBModuleSpecList.GetModuleSpecifications`)::

    spec = lldb.SBModuleSpec()
    spec.SetFileSpec(lldb.SBFileSpec('/path/to/libfoo.dylib'))
    spec.SetSymbolFileSpec(lldb.SBFileSpec('/path/to/libfoo.dylib.dSYM'))
    module = target.AddModule(spec)

See also :py:class:`SBModule` and :py:class:`SBModuleSpecList`."
) lldb::SBModuleSpec;

%feature("docstring",
"Returns whether this spec describes anything.

A default constructed spec is invalid until at least one of its properties is
set."
) lldb::SBModuleSpec::IsValid;

%feature("docstring",
"Resets this spec so that it matches nothing."
) lldb::SBModuleSpec::Clear;

%feature("docstring", "
    Get const accessor for the module file.

    This function returns the file for the module on the host system
    that is running LLDB. This can differ from the path on the
    platform since we might be doing remote debugging.

    :return: The file specification of the module.
    :rtype: SBFileSpec"
) lldb::SBModuleSpec::GetFileSpec;

%feature("docstring",
"Sets the path of the module on the host system, see
`SBModuleSpec.GetFileSpec`."
) lldb::SBModuleSpec::SetFileSpec;

%feature("docstring", "
    Get accessor for the module platform file.

    Platform file refers to the path of the module as it is known on
    the remote system on which it is being debugged. For local
    debugging this is always the same as Module::GetFileSpec(). But
    remote debugging might mention a file '/usr/lib/liba.dylib'
    which might be locally downloaded and cached. In this case the
    platform file could be something like:
    '/tmp/lldb/platform-cache/remote.host.computer/usr/lib/liba.dylib'
    The file could also be cached in a local developer kit directory.

    :return: The file specification of the module on the platform.
    :rtype: SBFileSpec"
) lldb::SBModuleSpec::GetPlatformFileSpec;

%feature("docstring",
"Sets the path of the module on the platform, see
`SBModuleSpec.GetPlatformFileSpec`."
) lldb::SBModuleSpec::SetPlatformFileSpec;

%feature("docstring",
"Returns the file that holds the debug information of the module.

See `SBModule.GetSymbolFileSpec`.

:rtype: SBFileSpec"
) lldb::SBModuleSpec::GetSymbolFileSpec;

%feature("docstring",
"Sets the file that holds the debug information of the module.

Use this to point LLDB at a separate symbol file, such as a dSYM bundle or a
``.debug`` file, that it could not find on its own."
) lldb::SBModuleSpec::SetSymbolFileSpec;

%feature("docstring",
"Returns the name of the object inside a larger file this spec refers to.

See `SBModule.GetObjectName`."
) lldb::SBModuleSpec::GetObjectName;

%feature("docstring",
"Sets the name of the object inside a larger file this spec refers to.

For example the name of a member of a static archive."
) lldb::SBModuleSpec::SetObjectName;

%feature("docstring",
"Returns the triple of this spec, e.g. ``x86_64-apple-macosx``."
) lldb::SBModuleSpec::GetTriple;

%feature("docstring",
"Sets the triple of this spec.

Use this to select one architecture of a universal binary::

    spec.SetTriple('arm64-apple-macosx')
"
) lldb::SBModuleSpec::SetTriple;

%feature("docstring",
"Returns the raw bytes of the UUID of this spec.

Use `SBModuleSpec.GetUUIDLength` to find out how many bytes there are. From
Python it is usually easier to compare `SBModule.GetUUIDString`."
) lldb::SBModuleSpec::GetUUIDBytes;

%feature("docstring",
"Returns the number of bytes of the UUID of this spec, or ``0`` if it has no
UUID."
) lldb::SBModuleSpec::GetUUIDLength;

%feature("docstring",
"Sets the UUID of this spec from raw bytes.

Setting a UUID makes the spec match only a module with exactly that UUID, which
is the most reliable way to identify a specific build of a binary."
) lldb::SBModuleSpec::SetUUIDBytes;

%feature("docstring",
"Returns the offset of the object inside the file this spec refers to."
) lldb::SBModuleSpec::GetObjectOffset;

%feature("docstring",
"Sets the offset of the object inside the file this spec refers to.

Used together with `SBModuleSpec.SetObjectSize` for files that contain several
objects."
) lldb::SBModuleSpec::SetObjectOffset;

%feature("docstring",
"Returns the size of the object inside the file this spec refers to."
) lldb::SBModuleSpec::GetObjectSize;

%feature("docstring",
"Sets the size of the object inside the file this spec refers to, see
`SBModuleSpec.SetObjectOffset`."
) lldb::SBModuleSpec::SetObjectSize;

%feature("docstring",
"Writes a description of this spec into the given `SBStream`."
) lldb::SBModuleSpec::GetDescription;

%feature("docstring",
"Returns the `SBTarget` that is used when resolving this spec.

See `SBModuleSpec.SetTarget`."
) lldb::SBModuleSpec::GetTarget;

%feature("docstring",
"Sets the target to be used when resolving a module.

A target can help locate a module specified by a SBModuleSpec. The
target settings, like the executable and debug info search paths, can
be essential. The target's platform can also be used to locate or download
the specified module."
) lldb::SBModuleSpec::SetTarget;

%feature("docstring",
"Represents a list of :py:class:`SBModuleSpec`.

The most common use is to find out what an object file on disk contains, for
example which architectures a universal binary has::

    for spec in lldb.SBModuleSpecList.GetModuleSpecifications('/usr/lib/dyld'):
        print(spec.GetTriple())

In Python the list supports ``len()``, indexing and iteration."
) lldb::SBModuleSpecList;

%feature("docstring",
"Returns the module specs of the object file at the given path.

This is a class method. Files such as universal Mach-O binaries and static
archives contain several objects, and this returns one spec per object."
) lldb::SBModuleSpecList::GetModuleSpecifications;

%feature("docstring",
"Appends a single `SBModuleSpec` or all specs of another `SBModuleSpecList` to
this list."
) lldb::SBModuleSpecList::Append;

%feature("docstring",
"Returns the first spec of this list that matches the given spec.

Only the properties that are set in ``match_spec`` have to match::

    match = lldb.SBModuleSpec()
    match.SetTriple('arm64-apple-macosx')
    spec = specs.FindFirstMatchingSpec(match)

Returns an invalid `SBModuleSpec` if nothing matches."
) lldb::SBModuleSpecList::FindFirstMatchingSpec;

%feature("docstring",
"Returns all specs of this list that match the given spec.

See `SBModuleSpecList.FindFirstMatchingSpec`.

:rtype: SBModuleSpecList"
) lldb::SBModuleSpecList::FindMatchingSpecs;

%feature("docstring",
"Returns the number of specs in this list.

In Python this is also what ``len()`` returns."
) lldb::SBModuleSpecList::GetSize;

%feature("docstring",
"Returns the spec at the given index as an `SBModuleSpec`."
) lldb::SBModuleSpecList::GetSpecAtIndex;

%feature("docstring",
"Writes a description of all specs in this list into the given `SBStream`."
) lldb::SBModuleSpecList::GetDescription;
