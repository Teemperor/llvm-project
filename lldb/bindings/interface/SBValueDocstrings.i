%feature("docstring",
"Represents the value of a variable, a register, or an expression.

An SBValue is a snapshot of some typed data in the target: a local variable, a
global, a register, a member of another value or the result of an expression.
Every value has a name (`SBValue.GetName`), a type (`SBValue.GetType`) and
possibly children (`SBValue.GetChildAtIndex`), which are themselves SBValue
objects.

Values are created by the objects they belong to, for example:

* `SBFrame.FindVariable`, `SBFrame.GetVariables` and `SBFrame.FindValue` for
  variables and registers of a stack frame.
* `SBFrame.EvaluateExpression` and `SBTarget.EvaluateExpression` for expression
  results.
* `SBTarget.FindFirstGlobalVariable` and `SBTarget.FindGlobalVariables` for
  global and static variables.
* `SBValue.CreateValueFromData` and `SBTarget.CreateValueFromData` to interpret
  raw bytes as a given type.

If a value could not be computed, the object is either invalid or
`SBValue.GetError` describes what went wrong::

    value = frame.EvaluateExpression('1 / 0')
    if value.GetError().Fail():
        print(value.GetError().GetCString())

Structs, classes, arrays and pointers have children that can be accessed by
index or by name. In Python an SBValue is iterable and indexable, so all of the
following are equivalent ways to reach a member::

    point.GetChildMemberWithName('x')
    point.member['x']
    point.GetChildAtIndex(0)
    point.child[0]
    list(point)[0]

Type summaries and synthetic children (see `SBTypeCategory`) change what the
children of a value look like. `SBValue.GetNonSyntheticValue` returns the value
without them applied, and `SBValue.GetDynamicValue` returns the value using its
runtime type instead of the type in the debug information.

For example, printing all general purpose registers of a frame::

    for regs in frame.registers:  # Returns an SBValueList.
        if 'general purpose registers' in regs.GetName().lower():
            for reg in regs:
                print('%s = %s' % (reg.GetName(), reg.GetValue()))

produces output such as::

    rax = 0x0000000100000c5c
    rbx = 0x0000000000000000
    rcx = 0x00007fff5fbffec0
    ...

The Python bindings add the properties documented in
:doc:`/python_extensions`, most importantly ``name``, ``type``, ``value``,
``summary``, ``children``, ``child``, ``member``, ``deref``, ``address_of``,
``unsigned``, ``signed``, ``data`` and ``error``. They also add
``linked_list_iter``, which walks a value as the head of a linked list::

    # Iterate a list of 'Task' objects that are chained via a 'next' member.
    for task in task_head.linked_list_iter('next'):
        print(task.GetChildMemberWithName('id').GetValueAsSigned())
"
) lldb::SBValue;

%feature("docstring",
"Returns whether this object holds a value.

An SBValue can be invalid because it was default constructed, because a lookup
such as `SBFrame.FindVariable` did not find anything, or because the expression
that was supposed to produce it failed. In the latter case `SBValue.GetError`
explains why."
) lldb::SBValue::IsValid;

%feature("docstring",
"Resets this object to an invalid, empty value."
) lldb::SBValue::Clear;

%feature("docstring",
"Returns an `SBError` describing the problem with reading this value.

The error is in the success state if the value was read successfully. Common
failures are variables that are optimized out, memory that cannot be read and
expressions that failed to parse or run."
) lldb::SBValue::GetError;

%feature("docstring",
"Returns the unique identifier LLDB assigned to this value.

The identifier is only unique within the process that created the value and is
mostly useful to detect that two SBValue objects refer to the same underlying
value."
) lldb::SBValue::GetID;

%feature("docstring",
"Returns the name of this value.

For variables this is the name in the source code, for registers the register
name and for expression results either the name that was passed to the
evaluating function or ``None``. Children of aggregates are named after the
member they represent, elements of arrays are named ``[0]``, ``[1]``, ..."
) lldb::SBValue::GetName;

%feature("docstring",
"Returns the name of this value's type as a string.

This is the same as ``GetType().GetName()``. See
`SBValue.GetDisplayTypeName` for the name that LLDB shows to users."
) lldb::SBValue::GetTypeName;

%feature("docstring",
"Returns the type name that LLDB displays for this value.

This can differ from `SBValue.GetTypeName`, for instance because a data
formatter provides a nicer name for the type or because the dynamic type of the
value is used."
) lldb::SBValue::GetDisplayTypeName;

%feature("docstring",
"Returns the size in bytes of this value.

This is the size of the value's type, see `SBType.GetByteSize`."
) lldb::SBValue::GetByteSize;

%feature("docstring",
"Returns whether this value is currently in scope.

Only meaningful for values that belong to a stack frame. A variable is out of
scope if the program counter of its frame is outside of the lexical block the
variable was declared in."
) lldb::SBValue::IsInScope;

%feature("docstring",
"Returns the format used when printing this value as one of the
``lldb.eFormat*`` enumerators.

See `SBValue.SetFormat` for changing it."
) lldb::SBValue::GetFormat;

%feature("docstring",
"Sets the format that `SBValue.GetValue` uses to print this value.

``format`` is one of the ``lldb.eFormat*`` enumerators::

    value.SetFormat(lldb.eFormatHex)
    print(value.GetValue())
"
) lldb::SBValue::SetFormat;

%feature("docstring",
"Returns a string representation of this value, or ``None``.

The string is formatted according to `SBValue.GetFormat`. Aggregates such as
structs and arrays usually don't have a value string; use
`SBValue.GetSummary` or iterate over their children instead.

Use `SBValue.GetValueAsSigned` or `SBValue.GetValueAsUnsigned` to get the value
as a number instead of a string."
) lldb::SBValue::GetValue;

%feature("docstring",
"Returns this value as a signed integer.

``fail_value`` is returned if the value cannot be converted to an integer. If
an `SBError` is passed it will describe the failure::

    error = lldb.SBError()
    number = value.GetValueAsSigned(error, -1)
"
) lldb::SBValue::GetValueAsSigned;

%feature("docstring",
"Returns this value as an unsigned integer.

``fail_value`` is returned if the value cannot be converted to an integer. If
an `SBError` is passed it will describe the failure.

For pointers this returns the raw pointer value including any bits that the
architecture uses for pointer authentication or memory tagging. Use
`SBValue.GetValueAsAddress` to get a value that can be used as an address."
) lldb::SBValue::GetValueAsUnsigned;

%feature("docstring", "
      Return the value as an address.  On failure, LLDB_INVALID_ADDRESS
      will be returned.  On architectures like AArch64, where the
      top (unaddressable) bits can be used for authentication,
      memory tagging, or top byte ignore,  this method will return
      the value with those top bits cleared.

      `SBValue.GetValueAsUnsigned` returns the actual value, with the
      authentication/Top Byte Ignore/Memory Tagging Extension bits.

      Calling this on a random value which is not a pointer is
      incorrect.  Call ``GetType().IsPointerType()`` if in doubt.

      An SB API program may want to show both the literal byte value
      and the address it refers to in memory.  These two SBValue
      methods allow SB API writers to behave appropriately for their
      interface."
) lldb::SBValue::GetValueAsAddress;

%feature("docstring",
"Returns what kind of value this is as one of the ``lldb.eValueType*``
enumerators.

This distinguishes for example local variables
(``lldb.eValueTypeVariableLocal``) from arguments
(``lldb.eValueTypeVariableArgument``), globals, registers and expression
results (``lldb.eValueTypeConstResult``)."
) lldb::SBValue::GetValueType;

%feature("docstring",
"Returns whether the value changed since the last time it was updated.

This compares against the value from the previous stop, so it always returns
``False`` for a newly created SBValue."
) lldb::SBValue::GetValueDidChange;

%feature("docstring",
"Returns the summary string for this value, or ``None``.

The summary is produced by the data formatter that applies to this value's
type, see `SBTypeSummary` and `SBTypeCategory`. For example a
``std::string`` typically has no value string but a summary showing its
contents.

The overload that takes an `SBStream` and `SBTypeSummaryOptions` writes the
summary into the stream and allows controlling how it is generated."
) lldb::SBValue::GetSummary;

%feature("docstring",
"Returns the language runtime's description of this value, or ``None``.

This is what the ``expression -O`` (``po``) command prints, e.g. the result of
calling ``description`` on an Objective-C object. Returns ``None`` for
languages and values that have no such concept."
) lldb::SBValue::GetObjectDescription;

%feature("docstring",
"Returns the value this value is a child of.

Returns an invalid value if this value has no parent, e.g. because it is a
variable or an expression result."
) lldb::SBValue::GetParent;

%feature("docstring",
"Returns this value using its dynamic (runtime) type.

For a variable declared as a base class pointer this returns a value with the
most specific type the runtime reports for the object. ``use_dynamic`` is one
of the ``lldb.eDynamic*`` enumerators; ``lldb.eDynamicCanRunTarget`` allows
LLDB to run code in the target to determine the type, whereas
``lldb.eDynamicDontRunTarget`` does not. Returns an invalid value if no dynamic
type could be determined.

In Python the ``dynamic`` property is a shortcut for
``GetDynamicValue(lldb.eDynamicCanRunTarget)``. See also
`SBValue.GetStaticValue`."
) lldb::SBValue::GetDynamicValue;

%feature("docstring",
"Returns this value using the static type from the debug information.

This is the inverse of `SBValue.GetDynamicValue`."
) lldb::SBValue::GetStaticValue;

%feature("docstring",
"Returns this value with synthetic children and formatters bypassed.

Useful to inspect the real members of a type for which a synthetic children
provider is registered, for example to see the internal fields of a
``std::vector`` instead of its elements. See `SBTypeSynthetic`."
) lldb::SBValue::GetNonSyntheticValue;

%feature("docstring",
"Returns this value with its synthetic children provider applied.

This is the inverse of `SBValue.GetNonSyntheticValue`. Returns an invalid value
if no synthetic children provider applies to this value's type."
) lldb::SBValue::GetSyntheticValue;

%feature("docstring",
"Returns whether this value prefers to be shown with its dynamic type.

The returned value is one of the ``lldb.eDynamic*`` enumerators. See
`SBValue.SetPreferDynamicValue`."
) lldb::SBValue::GetPreferDynamicValue;

%feature("docstring",
"Sets whether children of this value use their dynamic type.

``use_dynamic`` is one of the ``lldb.eDynamic*`` enumerators. This is the
setting that is inherited by any value derived from this one, for instance by
`SBValue.GetChildAtIndex`."
) lldb::SBValue::SetPreferDynamicValue;

%feature("docstring",
"Returns whether this value prefers to be shown with synthetic children.

See `SBValue.SetPreferSyntheticValue`."
) lldb::SBValue::GetPreferSyntheticValue;

%feature("docstring",
"Sets whether this value and its children use synthetic children providers.

Passing ``False`` has a similar effect to using `SBValue.GetNonSyntheticValue`,
but it also applies to any value derived from this one."
) lldb::SBValue::SetPreferSyntheticValue;

%feature("docstring",
"Returns whether this value is showing its dynamic (runtime) type.

See `SBValue.GetDynamicValue`."
) lldb::SBValue::IsDynamic;

%feature("docstring",
"Returns whether this value is produced by a synthetic children provider.

See `SBTypeSynthetic` and `SBValue.GetNonSyntheticValue`."
) lldb::SBValue::IsSynthetic;

%feature("docstring",
"Returns whether this value was created by a synthetic children provider.

Synthetic children providers written in Python mark the values they hand out
with `SBValue.SetSyntheticChildrenGenerated`."
) lldb::SBValue::IsSyntheticChildrenGenerated;

%feature("docstring",
"Marks this value as being created by a synthetic children provider.

Synthetic children providers should call this on values they create so that
LLDB knows they are not real members of the parent object. The Python
convenience functions ``synthetic_child_from_expression``,
``synthetic_child_from_data`` and ``synthetic_child_from_address`` do this
automatically."
) lldb::SBValue::SetSyntheticChildrenGenerated;

%feature("docstring",
"Returns a string describing where this value is stored.

This is typically a load address for values in memory, a register name for
values in registers or something like ``scalar`` for values that only exist in
the debugger. See `SBValue.GetAddress` and `SBValue.GetLoadAddress` for a
machine readable location."
) lldb::SBValue::GetLocation;

%feature("docstring",
"Writes a new value into the target by parsing the given string.

The string is interpreted according to the value's type, so ``'12'`` for an
integer or ``'0x1000'`` for a pointer. Returns ``False`` and fills in ``error``
if the write failed, for example because the value is not writable (see
`SBValue.CanSetValue`)::

    error = lldb.SBError()
    frame.FindVariable('i').SetValueFromCString('42', error)

See `SBValue.SetData` to write raw bytes instead."
) lldb::SBValue::SetValueFromCString;

%feature("docstring", "
    Returns whether this value can be modified through SetValueFromCString()
    or SetData().

    Returns False when the value is not writable. An example would be a
    variable value reconstructed from debug info via a computation or a constant.
    A True result does not guarantee a write will succeed; other
    runtime conditions may still prevent a successful write."
) lldb::SBValue::CanSetValue;

%feature("docstring",
"Returns the `SBTypeFormat` that applies to this value, if any."
) lldb::SBValue::GetTypeFormat;

%feature("docstring",
"Returns the `SBTypeSummary` that applies to this value, if any."
) lldb::SBValue::GetTypeSummary;

%feature("docstring",
"Returns the `SBTypeFilter` that applies to this value, if any."
) lldb::SBValue::GetTypeFilter;

%feature("docstring",
"Returns the `SBTypeSynthetic` that applies to this value, if any."
) lldb::SBValue::GetTypeSynthetic;

%feature("docstring",
"Overrides the `SBTypeSynthetic` chosen by the data formatter system for this
value.

This is useful in synthetic children providers where the children of an object
can only be determined by inspecting the object itself."
) lldb::SBValue::SetTypeSynthetic;

%feature("docstring",
"Returns the scripted object implementing this value's synthetic children.

Returns an `SBScriptObject` wrapping the Python object of the synthetic
children provider that produced this value. This is mostly useful for
debugging such providers."
) lldb::SBValue::GetTypeSyntheticImplementation;

%feature("docstring", "
    Get a child value by index from a value.

    Structs, unions, classes, arrays and pointers have child
    values that can be access by index.

    Structs and unions access child members using a zero based index
    for each child member.

    Classes reserve the first indexes for base classes that have
    members (empty base classes are omitted), and all members of the
    current class will then follow the base classes.

    Pointers differ depending on what they point to. If the pointer
    points to a simple type, the child at index zero
    is the only child value available, unless ``treat_as_array``
    is true, in which case the pointer will be used as an array
    and can create 'synthetic' child values using positive or
    negative indexes. If the pointer points to an aggregate type
    (an array, class, union, struct), then the pointee is
    transparently skipped and any children are going to be the indexes
    of the child values within the aggregate type. For example if
    we have a 'Point' type and we have a SBValue that contains a
    pointer to a 'Point' type, then the child at index zero will be
    the 'x' member, and the child at index 1 will be the 'y' member
    (the child at index zero won't be a 'Point' instance).

    If you actually need an SBValue that represents the type pointed
    to by a SBValue for which ``GetType().IsPointerType()`` returns true,
    regardless of the pointee type, you can do that with the
    `SBValue.Dereference` method (or the equivalent ``deref`` property).

    Arrays have a preset number of children that can be accessed by
    index and will returns invalid child values for indexes that are
    out of bounds unless ``treat_as_array`` is true. In this
    case the array can create 'synthetic' child values for indexes
    that aren't in the array bounds using positive or negative
    indexes.

    :param idx: The index of the child value to get.
    :param use_dynamic: One of the ``lldb.eDynamic*`` enumerators, specifying
        whether to get dynamic values and whether the target may be run to
        figure out the dynamic type of the child value.
    :param treat_as_array: If true, then allow child values to be created by
        index for pointers and arrays for indexes that normally wouldn't be
        allowed.
    :return: A new SBValue object that represents the child member value.
    :rtype: SBValue"
) lldb::SBValue::GetChildAtIndex;

%feature("docstring", "
    Returns the index of the child with the given name.

    Matches children of this object only and will match base classes and
    member names if this is a C/C++/Objective-C typed object.

    Returns ``lldb.UINT32_MAX`` if there is no child with that name.

    :param name: The name of the child value to look for.
    :return: The index that can be passed to `SBValue.GetChildAtIndex`."
) lldb::SBValue::GetIndexOfChildWithName;

%feature("docstring", "
    Returns the child member with the given name.

    Matches child members of this object and child members of any base
    classes. Returns an invalid value if there is no such member.

    :param name: The name of the child value to get.
    :param use_dynamic: One of the ``lldb.eDynamic*`` enumerators, specifying
        whether to get dynamic values and whether the target may be run to
        figure out the dynamic type of the child value.
    :return: A new SBValue object that represents the child member value.
    :rtype: SBValue"
) lldb::SBValue::GetChildMemberWithName;

%feature("docstring",
"Returns the value at the given expression path relative to this value.

The path is a chain of member accesses, dereferences and subscripts such as
``.a->b[0].c[1]->d``. Unlike `SBValue.EvaluateExpression` this does not compile
or run any code, it only walks the children of this value, which makes it
considerably cheaper. Returns an invalid value if the path cannot be
resolved::

    # Equivalent to task_head->next->id, without running any code.
    task_head.GetValueForExpressionPath('->next.id')
"
) lldb::SBValue::GetValueForExpressionPath;

%feature("docstring",
"Returns a value representing the address of this value.

The result has a pointer type and is invalid if this value is not stored in
memory. This is the equivalent of ``&value`` in C. In Python this is also
available as the ``address_of`` property."
) lldb::SBValue::AddressOf;

%feature("docstring",
"Returns the load address of this value as an integer.

Returns ``lldb.LLDB_INVALID_ADDRESS`` if the value is not in memory, e.g.
because it lives in a register. See `SBValue.GetAddress` for a section-relative
address."
) lldb::SBValue::GetLoadAddress;

%feature("docstring",
"Returns the address of this value as an `SBAddress`.

The returned address can be resolved to a symbol, module or line entry, see
`SBAddress.GetSymbolContext`. Returns an invalid address if the value is not
stored in memory."
) lldb::SBValue::GetAddress;

%feature("docstring",
"Creates a child value of the given type at a byte offset into this value.

This is useful to reinterpret parts of an object, for example in a synthetic
children provider::

    # Interpret the four bytes at offset 8 as an 'int' member called 'count'.
    int_type = value.GetTarget().GetBasicType(lldb.eBasicTypeInt)
    count = value.CreateChildAtOffset('count', 8, int_type)
"
) lldb::SBValue::CreateChildAtOffset;

%feature("docstring",
"Creates a value from the result of an expression, using this value as context.

The expression is evaluated as if it appeared in the scope of this value, so
members can be referred to by name. The result gets the given name. See
`SBValue.EvaluateExpression` for the same operation without naming the result."
) lldb::SBValue::CreateValueFromExpression;

%feature("docstring",
"Creates a value of the given type located at the given load address.

``address`` is a load address in the target::

    my_type = target.FindFirstType('MyStruct')
    value = some_value.CreateValueFromAddress('obj', 0x100000, my_type)

See `SBTarget.CreateValueFromAddress` for the same operation on a target."
) lldb::SBValue::CreateValueFromAddress;

%feature("docstring",
"Creates a value of the given type from raw bytes.

``data`` is an `SBData` holding the bytes of the value. The resulting value has
no address in the target, so `SBValue.GetAddress`, `SBValue.GetLoadAddress` and
`SBValue.AddressOf` all return invalid results for it.

See `SBTarget.CreateValueFromData` for the same operation on a target."
) lldb::SBValue::CreateValueFromData;

%feature("docstring",
"Creates a boolean value with the given name.

The resulting value has no address in the target. Mostly useful for synthetic
children providers that want to present a computed flag."
) lldb::SBValue::CreateBoolValue;

%feature("docstring", "
    Get an SBData wrapping what this SBValue points to.

    This method will dereference the current SBValue, if its
    data type is a ``T\*`` or ``T[]``, and extract ``item_count`` elements
    of type ``T`` from it, copying their contents in an :py:class:`SBData`.

    :param item_idx: The index of the first item to retrieve. For an array
        this is equivalent to array[item_idx], for a pointer
        to ``\*(pointer + item_idx)``. In either case, the measurement
        unit for item_idx is the ``sizeof(T)`` rather than the byte
    :param item_count: How many items should be copied into the output. By default
        only one item is copied, but more can be asked for.
    :return: The contents of the copied items on success. An empty :py:class:`SBData` otherwise.
    :rtype: SBData
    "
) lldb::SBValue::GetPointeeData;

%feature("docstring", "
    Get an SBData wrapping the contents of this SBValue.

    This method will read the contents of this object in memory
    and copy them into an SBData for future use.

    :return: An SBData with the contents of this SBValue on success, an empty
        :py:class:`SBData` otherwise.
    :rtype: SBData"
) lldb::SBValue::GetData;

%feature("docstring",
"Writes the given raw bytes into this value's storage in the target.

``data`` is an `SBData` whose size has to match the size of this value.
Returns ``False`` and fills in ``error`` on failure, for instance if the value
is not writable (see `SBValue.CanSetValue`). See
`SBValue.SetValueFromCString` to write a value given as a string instead."
) lldb::SBValue::SetData;

%feature("docstring",
"Returns a copy of this value with a different name.

The copy has this value as its parent. Use this when a value should be
presented under a different name without modifying the original value, for
example in a synthetic children provider."
) lldb::SBValue::Clone;

%feature("docstring",
"Returns the declaration of this value as an `SBDeclaration`.

The declaration contains the file, line and column where the variable was
declared. Only available for values that come from debug information."
) lldb::SBValue::GetDeclaration;

%feature("docstring",
"Returns whether this value might have children.

This is much cheaper than `SBValue.GetNumChildren` because it does not need to
complete the value's type. It returns ``True`` for classes, unions, structs,
pointers, references, arrays and similar types, which makes it a good fit for
deciding whether a UI should show a disclosure triangle for a value."
) lldb::SBValue::MightHaveChildren;

%feature("docstring",
"Returns whether this value is an artificial value added by a language runtime.

Examples are the metadata variables that Swift or Objective-C runtimes add to a
frame. Such values are hidden by default in the command line interface and
usually should not be shown to users."
) lldb::SBValue::IsRuntimeSupportValue;

%feature("docstring", "
    Returns the number of children of this value.

    Note that for some values this operation can be expensive because it has to
    complete the value's type or run a synthetic children provider. If you only
    care about a limited number of children, pass ``max``: the returned value is
    then capped and no more children than needed are computed.

    If the value returned for a given ``max`` is smaller than ``max`` it is the
    true number of children, otherwise there are at least ``max`` children.

    See `SBValue.MightHaveChildren` for a cheap check whether a value has
    children at all."
) lldb::SBValue::GetNumChildren;

%feature("docstring",
"Returns the `SBTarget` this value belongs to."
) lldb::SBValue::GetTarget;

%feature("docstring",
"Returns the `SBProcess` this value belongs to.

The result may be invalid, for instance for values that were read from a target
without a running process."
) lldb::SBValue::GetProcess;

%feature("docstring",
"Returns the `SBThread` this value belongs to.

The result is invalid for values that are not tied to a thread, such as global
variables."
) lldb::SBValue::GetThread;

%feature("docstring",
"Returns the `SBFrame` this value belongs to.

The result is invalid for values that are not tied to a stack frame, such as
global variables."
) lldb::SBValue::GetFrame;

%feature("docstring",
"Returns the value this pointer or reference points to.

This is the equivalent of ``*value`` in C and is available as the ``deref``
property in Python. Returns an invalid value if this value is neither a pointer
nor a reference. See `SBValue.AddressOf` for the opposite operation."
) lldb::SBValue::Dereference;

%feature("docstring",
"Returns the type of this value as an `SBType`."
) lldb::SBValue::GetType;

%feature("docstring",
"Creates a persistent copy of this value in the target.

The returned value keeps working after the process resumes or exits and is
available in the expression evaluator under a name like ``$0``. This is what
the ``expression`` command does when it prints results with a ``$`` prefix."
) lldb::SBValue::Persist;

%feature("docstring",
"Writes a description of this value into the given `SBStream`.

The description contains the name, type, value and, depending on the
description level, the children of this value."
) lldb::SBValue::GetDescription;

%feature("docstring",
"Returns the expression path of this value.

The expression path is a string such as ``task_head->next->id`` that can be
passed to `SBFrame.EvaluateExpression` to get to this value again. The overload
that takes an `SBStream` writes the path into that stream and returns whether
it succeeded; in Python the ``path`` property returns the path as a string."
) lldb::SBValue::GetExpressionPath;

%feature("docstring",
"Evaluates an expression in the context of this value.

The expression is evaluated as if it appeared inside the scope of this value,
so its members are visible without qualification. This is a convenient way to
call a method on an object::

    # Equivalent to running 'expr obj.size()' in the frame of 'obj'.
    size = obj.EvaluateExpression('size()')

``options`` is an `SBExpressionOptions` that controls how the expression is run.
See also `SBFrame.EvaluateExpression` and `SBTarget.EvaluateExpression`."
) lldb::SBValue::EvaluateExpression;

%feature("docstring", "
    Sets a watchpoint on this value and returns it as an `SBWatchpoint`.

    The value has to be stored in memory and small enough to be watched by the
    hardware of the target. The returned watchpoint may be invalid if no
    watchpoint could be set, in which case ``error`` describes why.

    :param resolve_location: Resolve the location of this value once and watch
        its address. This currently has to be ``True``, watching all locations
        of a variable is not supported yet.
    :param read: Stop when the value is read.
    :param write: Stop when the value is written to.
    :param error: An `SBError` that is filled in on failure.
    :rtype: SBWatchpoint

    For example,::

        error = lldb.SBError()
        watchpoint = frame.FindVariable('global_counter').Watch(True, False, True, error)

    See `SBTarget.WatchpointCreateByAddress` for setting watchpoints on
    arbitrary addresses."
) lldb::SBValue::Watch;

%feature("docstring", "
    Sets a watchpoint on the memory this value points to.

    Behaves like `SBValue.Watch`, except that the watched location is what this
    pointer points to instead of the pointer itself.

    :rtype: SBWatchpoint"
) lldb::SBValue::WatchPointee;

%feature("docstring",
"Returns the virtual function table of this C++ object.

`SBValue.GetError` of the returned value is in the success state if this value
is a C++ class with a vtable and describes the problem otherwise.

The returned value has these properties:

* `SBValue.GetName` is the demangled symbol name of the table, e.g.
  ``vtable for MyClass``.
* `SBValue.GetValueAsUnsigned` and `SBValue.GetValue` are the address of the
  first vtable entry.
* `SBValue.GetLoadAddress` is the address of the vtable pointer inside the
  object.
* `SBValue.GetNumChildren` is the number of virtual function pointers, and
  each child is one virtual function pointer.

The children are named ``[0]``, ``[1]``, ... and their value is the address of
the virtual function they point to. For example, listing the virtual functions
of an object::

    vtable = value.GetVTable()
    for entry in vtable:
        addr = target.ResolveLoadAddress(entry.GetValueAsUnsigned())
        print('%s: %s' % (entry.GetName(), addr.GetSymbol().GetName()))
"
) lldb::SBValue::GetVTable;

%feature("docstring",
"Deprecated, use the expression evaluator to perform type casting.

`SBValue.CreateValueFromAddress` or
`SBFrame.EvaluateExpression` with a cast expression are the supported ways to
reinterpret a value as a different type."
) lldb::SBValue::Cast;

%feature("docstring",
"Deprecated, use `SBValue.GetType` instead."
) lldb::SBValue::GetOpaqueType;

%feature("docstring",
"Deprecated, use ``GetType().IsPointerType()`` instead."
) lldb::SBValue::TypeIsPointerType;
