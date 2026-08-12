%feature("docstring",
"Represents a field, a base class or an ivar of a type.

Members are returned by `SBType.GetFieldAtIndex`,
`SBType.GetDirectBaseClassAtIndex` and `SBType.GetVirtualBaseClassAtIndex`. A
member has a name, a type and an offset inside the containing type::

    task_type = target.FindFirstType('Task')
    for i in range(task_type.GetNumberOfFields()):
        field = task_type.GetFieldAtIndex(i)
        print('%s %s at offset %d' % (field.GetType().GetName(), field.GetName(),
                                      field.GetOffsetInBytes()))

See `SBTypeStaticField` for static data members, which are not part of the
fields of a type."
) lldb::SBTypeMember;

%feature("docstring",
"Returns the name of this member.

Returns an empty string for anonymous members. For base classes this is the
name of the base class type."
) lldb::SBTypeMember::GetName;

%feature("docstring",
"Returns the type of this member as an `SBType`."
) lldb::SBTypeMember::GetType;

%feature("docstring",
"Returns the offset of this member inside the containing type in bytes.

For bitfields this is the offset of the byte the bitfield starts in; use
`SBTypeMember.GetOffsetInBits` to get the exact position."
) lldb::SBTypeMember::GetOffsetInBytes;

%feature("docstring",
"Returns the offset of this member inside the containing type in bits."
) lldb::SBTypeMember::GetOffsetInBits;

%feature("docstring",
"Returns whether this member is a bitfield.

See `SBTypeMember.GetBitfieldSizeInBits` for the declared width of the
bitfield."
) lldb::SBTypeMember::IsBitfield;

%feature("docstring",
"Returns the width of this bitfield in bits.

Returns ``0`` if this member is not a bitfield, see
`SBTypeMember.IsBitfield`."
) lldb::SBTypeMember::GetBitfieldSizeInBits;

%feature("docstring",
"Represents a member function (a method) of a type.

Member functions are returned by `SBType.GetMemberFunctionAtIndex`. Note that
this only describes the function's signature, it is not a callable object; use
`SBFrame.EvaluateExpression` or `SBValue.EvaluateExpression` to actually call a
function in the target::

    task_type = target.FindFirstType('Task')
    for i in range(task_type.GetNumberOfMemberFunctions()):
        method = task_type.GetMemberFunctionAtIndex(i)
        print('%s %s' % (method.GetReturnType().GetName(), method.GetName()))
"
) lldb::SBTypeMemberFunction;

%feature("docstring",
"Returns the name of this member function."
) lldb::SBTypeMemberFunction::GetName;

%feature("docstring",
"Returns the demangled name of this member function, if it has one."
) lldb::SBTypeMemberFunction::GetDemangledName;

%feature("docstring",
"Returns the mangled name of this member function, if it has one.

Returns ``None`` for languages that don't mangle names or if the debug
information doesn't contain the mangled name."
) lldb::SBTypeMemberFunction::GetMangledName;

%feature("docstring",
"Returns the function type of this member function as an `SBType`."
) lldb::SBTypeMemberFunction::GetType;

%feature("docstring",
"Returns the return type of this member function as an `SBType`."
) lldb::SBTypeMemberFunction::GetReturnType;

%feature("docstring",
"Returns the number of arguments of this member function.

For non-static member functions this does not include the implicit ``this``
argument."
) lldb::SBTypeMemberFunction::GetNumberOfArguments;

%feature("docstring",
"Returns the type of the argument with the given index.

Returns an invalid `SBType` if the index is out of range. See
`SBTypeMemberFunction.GetNumberOfArguments`."
) lldb::SBTypeMemberFunction::GetArgumentTypeAtIndex;

%feature("docstring",
"Returns what kind of member function this is.

The result is one of the ``lldb.eMemberFunctionKind*`` enumerators, which
distinguishes constructors, destructors, static methods and instance methods."
) lldb::SBTypeMemberFunction::GetKind;

%feature("docstring",
"Represents a static data member of a type.

Static data members are not part of the fields of a type (they are not stored
inside instances), so they are looked up by name with
`SBType.GetStaticFieldWithName`::

    static_field = my_type.GetStaticFieldWithName('s_instance_count')
    value = static_field.GetConstantValue(target)
"
) lldb::SBTypeStaticField;

%feature("docstring",
"Returns the name of this static data member."
) lldb::SBTypeStaticField::GetName;

%feature("docstring",
"Returns the mangled name of this static data member, if it has one."
) lldb::SBTypeStaticField::GetMangledName;

%feature("docstring",
"Returns the type of this static data member as an `SBType`."
) lldb::SBTypeStaticField::GetType;

%feature("docstring",
"Returns the value of this static data member as an `SBValue`.

``target`` is the `SBTarget` in which the value should be read. Returns an
invalid value if the member has no value that LLDB can read, for instance
because it is only declared and never defined."
) lldb::SBTypeStaticField::GetConstantValue;

%feature("docstring",
"Represents a data type in lldb.

The actual characteristics of each type are defined by the semantics of the
programming language and the specific language implementation that was used
to compile the target program. See the language-specific notes in the
documentation of each method.

SBType instances can be obtained by a variety of methods.
`SBTarget.FindFirstType` and `SBModule.FindFirstType` can be used to create
`SBType` representations of types in executables/libraries with debug
information. For some languages such as C, C++ and Objective-C it is possible
to create new types by evaluating expressions that define a new type.

Note that most `SBType` properties are computed independently of any runtime
information so for dynamic languages the functionality can be very limited.
`SBValue` can be used to represent runtime values which then can be more
accurately queried for certain information such as byte size.

Types can be inspected and transformed with the methods below, and two types
can be compared with the ``==`` and ``!=`` operators. For example::

    # A pointer type and the type of a variable of that type are the same type.
    task_type = target.FindFirstType('Task')
    task_head = frame.FindVariable('task_head')  # Declared as 'Task *'.
    assert task_head.GetType() == task_type.GetPointerType()

    # Walk to the pointee type and list its fields.
    for i in range(task_head.GetType().GetPointeeType().GetNumberOfFields()):
        field = task_head.GetType().GetPointeeType().GetFieldAtIndex(i)
        print('%s %s' % (field.GetType().GetName(), field.GetName()))

The ``Is*`` methods (`SBType.IsPointerType`, `SBType.IsAggregateType`, ...)
classify a type, the ``Get*Type`` methods navigate between related types
(`SBType.GetPointerType`, `SBType.GetPointeeType`,
`SBType.GetCanonicalType`, ...) and `SBType.GetFieldAtIndex`,
`SBType.GetMemberFunctionAtIndex` and `SBType.GetEnumMembers` inspect the
contents of a type.

See also `SBTypeList`, which is what the ``FindTypes`` functions return, and
`SBTypeCategory` for changing how values of a type are displayed.
") lldb::SBType;

%feature("docstring",
    "Returns whether this object represents a type.

    An SBType is invalid if it was default constructed or if the lookup that
    was supposed to produce it (for example `SBTarget.FindFirstType`) found
    nothing. Most methods of an invalid type return an invalid type, ``0`` or
    an empty string.
    "
) lldb::SBType::IsValid;

%feature("docstring",
    "Returns the number of bytes a variable with the given types occupies in memory.

    Returns ``0`` if the size can't be determined.

    If a type occupies ``N`` bytes + ``M`` bits in memory, this function returns
    the rounded up amount of bytes (i.e., if ``M`` is ``0``,
    this function returns ``N`` and otherwise ``N + 1``).

    Language-specific behaviour:

    * C: The output is expected to match the value of ``sizeof(Type)``. If
      ``sizeof(Type)`` is not a valid expression for the given type, the
      function returns ``0``.
    * C++: Same as in C.
    * Objective-C: Same as in C. For Objective-C classes this always returns
      ``0`` as the actual size depends on runtime information.
    "
) lldb::SBType::GetByteSize;

%feature("docstring",
    "Returns the alignment requirement of this type in bytes.

    Returns ``0`` if the alignment can't be determined.

    Language-specific behaviour:

    * C: The output is expected to match the value of ``_Alignof(Type)``.
    * C++: Same as in C (``alignof(Type)``).
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetByteAlign;

%feature("docstring",
    "Returns true if this type is a function type.

    Language-specific behaviour:

    * C: Returns true for function types such as ``int(int)``. Pointers to
      functions are not function types, use `SBType.GetPointeeType` on them
      first.
    * C++: Same as in C. Member functions are also considered function types.
    * Objective-C: Same as in C. Objective-C methods are not function types.

    See `SBType.GetFunctionReturnType` and `SBType.GetFunctionArgumentTypes` to
    inspect the signature of a function type.
    "
) lldb::SBType::IsFunctionType;

%feature("docstring",
    "Returns true if this type is a pointer type.

    Language-specific behaviour:

    * C: Returns true for C pointer types (or typedefs of these types).
    * C++: Pointer types include the C pointer types as well as pointers to data
      mebers or member functions.
    * Objective-C: Pointer types include the C pointer types. ``id``, ``Class``
      and pointers to blocks are also considered pointer types.
    "
) lldb::SBType::IsPointerType;

%feature("docstring",
    "Returns true if this type is a reference type.

    Language-specific behaviour:

    * C: Returns false for all types.
    * C++: Both l-value and r-value references are considered reference types.
    * Objective-C: Returns false for all types.
    "
) lldb::SBType::IsReferenceType;

%feature("docstring",
    "Returns true if this type is a polymorphic type.

    Language-specific behaviour:

    * C: Returns false for all types.
    * C++: Returns true if the type is a class type that contains at least one
      virtual member function or if at least one of its base classes is
      considered a polymorphic type.
    * Objective-C: Returns false for all types.
    "
) lldb::SBType::IsPolymorphicClass;

%feature("docstring",
    "Returns true if this type is an array type.

    Language-specific behaviour:

    * C: Returns true if the types is an array type. This includes incomplete
      array types ``T[]`` and array types with integer (``T[1]``) or variable
      length (``T[some_variable]``). Pointer types are not considered arrays.
    * C++: Includes C's array types and dependent array types (i.e., array types
      in templates which size depends on template arguments).
    * Objective-C: Same as in C.
    "
) lldb::SBType::IsArrayType;

%feature("docstring",
    "Returns true if this type is a vector type.

    Language-specific behaviour:

    * C: Returns true if the types is a vector type created with
      GCC's ``vector_size`` or Clang's ``ext_vector_type`` feature.
    * C++: Same as in C.
    * Objective-C: Same as in C.
    "
) lldb::SBType::IsVectorType;

%feature("docstring",
    "Returns true if this type is a typedef.

    Language-specific behaviour:

    * C: Returns true if the type is a C typedef.
    * C++: Same as in C. Also treats type aliases as typedefs.
    * Objective-C: Same as in C.
    "
) lldb::SBType::IsTypedefType;

%feature("docstring",
    "Returns true if this type is an anonymous type.

    Language-specific behaviour:

    * C: Returns true for anonymous unions. Also returns true for
      anonymous structs (which are a GNU language extension).
    * C++: Same as in C.
    * Objective-C: Same as in C.
    "
) lldb::SBType::IsAnonymousType;

%feature("docstring",
    "Returns true if this type is a scoped enum.

    Language-specific behaviour:

    * C: Returns false for all types.
    * C++: Return true only for C++11 scoped enums.
    * Objective-C: Returns false for all types.
    "
) lldb::SBType::IsScopedEnumerationType;

%feature("docstring",
    "Returns true if this type is an aggregate type.

    Language-specific behaviour:

    * C: Returns true for struct values, arrays, and vectors.
    * C++: Same a C. Also includes class instances.
    * Objective-C: Same as C. Also includes class instances.
    "
) lldb::SBType::IsAggregateType;

%feature("docstring",
    "Returns a type that represents a pointer to this type.

    If the type system of the current language can't represent a pointer to this
    type or this type is invalid, an invalid `SBType` is returned.

    Language-specific behaviour:

    * C: Returns the pointer type of this type.
    * C++: Same as in C.
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetPointerType;

%feature("docstring",
    "Returns the underlying pointee type.

    If this type is a pointer type as specified by `IsPointerType` then this
    returns the underlying type. If this is not a pointer type or an invalid
    `SBType` then this returns an invalid `SBType`.

    Language-specific behaviour:

    * C: Returns the underlying type for for C pointer types or typedefs of
      these types). For example, ``int *`` will return ``int``.
    * C++: Same as in C. Returns an `SBType` representation for data members/
      member functions in case the `SBType` is a pointer to data member or
      pointer to member function.
    * Objective-C: Same as in C. The pointee type of ``id`` and ``Class`` is
      an invalid `SBType`. The pointee type of pointers Objective-C types is an
      `SBType` for the non-pointer type of the respective type. For example,
      ``NSString *`` will return ``NSString`` as a pointee type.
    "
) lldb::SBType::GetPointeeType;

%feature("docstring",
    "Returns a type that represents a reference to this type.

    If the type system of the current language can't represent a reference to
    this type, an invalid `SBType` is returned.

    Language-specific behaviour:

    * C: Currently assumes the type system is C++ and returns an l-value
      reference type. For example, ``int`` will return ``int&``. This behavior
      is likely to change in the future and shouldn't be relied on.
    * C++: Same as in C.
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetReferenceType;

%feature("docstring",
    "Returns the underlying type of a typedef.

    If this type is a typedef as designated by `IsTypedefType`, then the
    underlying type is being returned. Otherwise an invalid `SBType` is
    returned.

    Language-specific behaviour:

    * C: Returns the underlying type of a typedef type.
    * C++: Same as in C. For type aliases, the underlying type is returned.
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetTypedefedType;

%feature("docstring",
    "Returns the underlying type of a reference type.

    If this type is a reference as designated by `IsReferenceType`, then the
    underlying type is being returned. Otherwise an invalid `SBType` is
    returned.

    Language-specific behaviour:

    * C: Always returns an invalid type.
    * C++: For l-value and r-value references the underlying type is returned.
      For example, ``int &`` will return ``int``.
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetDereferencedType;

%feature("docstring",
    "Returns the unqualified version of this type.

    Language-specific behaviour:

    * C: If this type with any const or volatile specifier removed.
    * C++: Same as in C.
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetUnqualifiedType;

%feature("docstring",
    "Returns the canonical version of this type.

    The canonical type is the type with all typedefs and type aliases resolved.
    In contrast to `SBType.GetTypedefedType` this looks through all levels of
    typedefs at once and also resolves typedefs that are nested inside the type
    (for example the element type of an array).

    Language-specific behaviour:

    * C: For a typedef this returns the type it ultimately refers to, for any
      other type it returns the type itself.
    * C++: Same as in C. Also resolves type aliases and ``decltype``
      expressions.
    * Objective-C: Same as in C.

    See also `SBType.GetUnqualifiedType`, which only removes ``const`` and
    ``volatile``.
    "
) lldb::SBType::GetCanonicalType;

%feature("docstring",
    "Returns the underlying integer type if this is an enumeration type.

    If this type is an invalid `SBType` or not an enumeration type an invalid
    `SBType` is returned.

    Language-specific behaviour:

    * C: Returns the underlying type for enums.
    * C++: Same as in C but also returns the underlying type for scoped enums.
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetEnumerationIntegerType;

%feature("docstring",
    "Returns the array element type if this type is an array type.

    Otherwise returns an invalid `SBType` if this type is invalid or not an
    array type.

    Language-specific behaviour:

    * C: If this is an array type (see `IsArrayType`) such as ``T[]``, returns
      the element type.
    * C++: Same as in C.
    * Objective-C: Same as in C.

    See also `IsArrayType`.
    "
) lldb::SBType::GetArrayElementType;

%feature("docstring",
    "Returns the array type with the given constant size.

    Language-specific behaviour:

    * C: Returns a constant-size array ``T[size]`` for any non-void type.
    * C++: Same as in C.
    * Objective-C: Same as in C.

    See also `IsArrayType` and `GetArrayElementType`.
    "
) lldb::SBType::GetArrayType;

%feature("docstring",
    "Returns the vector element type if this type is a vector type.

    Otherwise returns an invalid `SBType` if this type is invalid or not a
    vector type.

    Language-specific behaviour:

    * C: If this is a vector type (see `IsVectorType`), returns the element
      type.
    * C++: Same as in C.
    * Objective-C: Same as in C.

    See also `IsVectorType`.
    "
) lldb::SBType::GetVectorElementType;

%feature("docstring",
    "Returns the `BasicType` value that is most appropriate to this type.

    Returns `eBasicTypeInvalid` if no appropriate `BasicType` was found or this
    type is invalid. See the `BasicType` documentation for the language-specific
    meaning of each `BasicType` value.

    **Overload behaviour:** When called with a `BasicType` parameter, the
    following behaviour applies:

    Returns the `SBType` that represents the passed `BasicType` value. Returns
    an invalid `SBType` if no fitting `SBType` could be created.

    Language-specific behaviour:

    * C: Returns the respective builtin type. Note that some types
      (e.g. ``__uint128_t``) might even be successfully created even if they are
      not available on the target platform. C++ and Objective-C specific types
      might also be created even if the target program is not written in C++ or
      Objective-C.
    * C++: Same as in C.
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetBasicType;

%feature("docstring",
    "Returns the number of fields of this type.

    Returns ``0`` if this type does not have fields.

    Language-specific behaviour:

    * C: Returns the number of fields if the type is a struct. If the type
      contains an anonymous struct/union it only counts as a single field (even
      if the struct/union contains several fields).
    * C++: Returns the number of non-static fields if the type is a
      struct/class. If the type contains an anonymous struct/union it only
      counts as a single field (even if the struct/union contains several
      fields). The fields of any base classes are not included in the count.
    * Objective-C: Same as in C for structs. For Objective-C classes the number
      of ivars is returned.

    See also `GetFieldAtIndex`.
    "
) lldb::SBType::GetNumberOfFields;

%feature("docstring",
    "Returns the number of base/parent classes of this type.

    Returns ``0`` if this type doesn't have any base classes.

    Language-specific behaviour:

    * C: Returns always ``0``.
    * C++: The number of direct non-virtual base classes if this type is
      a class.
    * Objective-C: The number of super classes for Objective-C classes.
      As Objective-C doesn't have multiple inheritance this is usually returns 1
      except for NSObject.
    "
) lldb::SBType::GetNumberOfDirectBaseClasses;

%feature("docstring",
    "Returns the number of virtual base/parent classes of this type

    Returns ``0`` if this type doesn't have any base classes.

    Language-specific behaviour:

    * C: Returns always ``0``.
    * C++: The number of direct virtual base classes if this type is a
      class.
    * Objective-C: Returns always ``0``.
    "
) lldb::SBType::GetNumberOfVirtualBaseClasses;

%feature("docstring",
    "Returns the field at the given index.

    Returns an invalid `SBType` if the index is out of range or the current
    type doesn't have any fields.

    Language-specific behaviour:

    * C: Returns the field with the given index for struct types. Fields are
      ordered/indexed starting from ``0`` for the first field in a struct (as
      declared in the definition).
    * C++: Returns the non-static field with the given index for struct types.
      Fields are ordered/indexed starting from ``0`` for the first field in a
      struct (as declared in the definition).
    * Objective-C: Same as in C for structs. For Objective-C classes the ivar
      with the given index is returned. ivars are indexed starting from ``0``.
    "
) lldb::SBType::GetFieldAtIndex;

%feature("docstring",
    "Returns the direct base class as indexed by `GetNumberOfDirectBaseClasses`.

    Returns an invalid SBTypeMember if the index is invalid or this SBType is
    invalid.
    "
) lldb::SBType::GetDirectBaseClassAtIndex;

%feature("docstring",
    "Returns the virtual base class as indexed by
    `GetNumberOfVirtualBaseClasses`.

    Returns an invalid SBTypeMember if the index is invalid or this SBType is
    invalid.
    "
) lldb::SBType::GetVirtualBaseClassAtIndex;

%feature("docstring",
    "Returns the static data member with the given name.

    Static data members are not part of the fields of a type, so they can't be
    found via `SBType.GetFieldAtIndex`. Returns an invalid `SBTypeStaticField`
    if this type has no static data member with that name.

    Language-specific behaviour:

    * C: Always returns an invalid `SBTypeStaticField`.
    * C++: Returns ``static`` data members of classes and structs.
    * Objective-C: Returns class variables.
    "
) lldb::SBType::GetStaticFieldWithName;

%feature("docstring",
    "Returns the members of this enumeration type.

    Returns an `SBTypeEnumMemberList`, which is empty if this type is not an
    enumeration type. For example::

        for member in my_enum_type.GetEnumMembers():
            print('%s = %d' % (member.GetName(), member.GetValueAsSigned()))

    See also `SBType.GetEnumerationIntegerType` and
    `SBType.IsScopedEnumerationType`.
    "
) lldb::SBType::GetEnumMembers;

%feature("docstring",
    "Returns the `SBModule` this `SBType` belongs to.

    Returns no `SBModule` if this type does not belong to any specific
    `SBModule` or this `SBType` is invalid. An invalid `SBModule` might also
    indicate that once came from an `SBModule` but LLDB could no longer
    determine the original module.
    "
) lldb::SBType::GetModule;

%feature("autodoc", "GetName() -> string") lldb::SBType::GetName;

%feature("docstring",
    "Returns the name of this type.

    Returns an empty string if an error occurred or this type is invalid.

    Use this function when trying to match a specific type by name in a script.
    The names returned by this function try to uniquely identify a name but
    conflicts can occur (for example, if a C++ program contains two different
    classes with the same name in different translation units. `GetName` can
    return the same name for both class types.)


    Language-specific behaviour:

    * C: The name of the type. For structs the ``struct`` prefix is omitted.
    * C++: Returns the qualified name of the type (including anonymous/inline
      namespaces and all template arguments).
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetName;

%feature("autodoc", "GetDisplayTypeName() -> string") lldb::SBType::GetDisplayTypeName;

%feature("docstring",
    "Returns the name of this type in a user-friendly format.

    Returns an empty string if an error occurred or this type is invalid.

    Use this function when displaying a type name to the user.

    Language-specific behaviour:

    * C: Returns the type name. For structs the ``struct`` prefix is omitted.
    * C++: Returns the qualified name. Anonymous/inline namespaces are omitted.
      Template arguments that match their default value might also be hidden
      (this functionality depends on whether LLDB can determine the template's
      default arguments).
    * Objective-C: Same as in C.
    "
) lldb::SBType::GetDisplayTypeName;

%feature("autodoc", "GetTypeClass() -> TypeClass") lldb::SBType::GetTypeClass;

%feature("docstring",
    "Returns the `TypeClass` for this type.

    Returns an `eTypeClassInvalid` if this `SBType` is invalid.

    See `TypeClass` for the language-specific meaning of each `TypeClass` value.
    "
) lldb::SBType::GetTypeClass;

%feature("docstring",
    "Returns the number of template arguments of this type.

    Returns ``0`` if this type is not a template.

    Language-specific behaviour:

    * C: Always returns ``0``.
    * C++: If this type is a class template instantiation then this returns the
      number of template parameters that were used in this instantiation. This
      includes both explicit and implicit template parameters.
    * Objective-C: Always returns ``0``.
    "
) lldb::SBType::GetNumberOfTemplateArguments;

%feature("docstring",
    "Returns the type of the template argument with the given index.

    Returns an invalid `SBType` if there is no template argument with the given
    index or this type is not a template. The first template  argument has the
    index ``0``.

    Language-specific behaviour:

    * C: Always returns an invalid SBType.
    * C++: If this type is a class template instantiation and the template
      parameter with the given index is a type template parameter, then this
      returns the type of that parameter. Otherwise returns an invalid `SBType`.
    * Objective-C: Always returns an invalid SBType.
    "
) lldb::SBType::GetTemplateArgumentType;

%feature("docstring",
    "Returns the value of the non-type template argument with the given index.

    ``target`` is the `SBTarget` that provides the context in which the value is
    created. Returns an invalid `SBValue` if the index is out of bounds or the
    template argument is not a value (for example because it is a type). Packs
    of template arguments are expanded, so each element of a pack has its own
    index.

    Language-specific behaviour:

    * C: Always returns an invalid `SBValue`.
    * C++: For an instantiation of ``template <int N> struct Array {};`` as
      ``Array<42>``, ``GetTemplateArgumentValue(target, 0)`` returns a value
      holding ``42``.
    * Objective-C: Always returns an invalid `SBValue`.

    See `SBType.GetTemplateArgumentKind` to find out whether an argument is a
    type or a value.
    "
) lldb::SBType::GetTemplateArgumentValue;

%feature("docstring",
    "Returns the kind of the template argument with the given index.

    Returns `eTemplateArgumentKindNull` if there is no template argument
    with the given index or this type is not a template. The first template
    argument has the index ``0``.

    Language-specific behaviour:

    * C: Always returns `eTemplateArgumentKindNull`.
    * C++: If this type is a class template instantiation then this returns
      the appropriate `TemplateArgument` value for the parameter with the given
      index. See the documentation of `TemplateArgument` for how certain C++
      template parameter kinds are mapped to `TemplateArgument` values.
    * Objective-C: Always returns `eTemplateArgumentKindNull`.
    "
) lldb::SBType::GetTemplateArgumentKind;

%feature("docstring",
    "Returns the return type if this type represents a function.

    Returns an invalid `SBType` if this type is not a function type or invalid.

    Language-specific behaviour:

    * C: For functions return the return type. Returns an invalid `SBType` if
      this type is a function pointer type.
    * C++: Same as in C for functions and instantiated template functions.
      Member functions are also considered functions. For functions that have
      their return type specified by a placeholder type specifier (``auto``)
      this returns the deduced return type.
    * Objective-C: Same as in C for functions. For Objective-C methods this
      returns the return type of the method.
    "
) lldb::SBType::GetFunctionReturnType;

%feature("docstring",
    "Returns the list of argument types if this type represents a function.

    Returns an invalid `SBType` if this type is not a function type or invalid.

    Language-specific behaviour:

    * C: For functions return the types of each parameter. Returns an invalid
      `SBType` if this type is a function pointer. For variadic functions this
      just returns the list of parameters before the variadic arguments.
    * C++: Same as in C for functions and instantiated template functions.
      Member functions are also considered functions.
    * Objective-C: Always returns an invalid SBType for Objective-C methods.
    "
) lldb::SBType::GetFunctionArgumentTypes;

%feature("docstring",
    "Returns the number of member functions of this type.

    Returns ``0`` if an error occurred or this type is invalid.

    Language-specific behaviour:

    * C: Always returns ``0``.
    * C++: If this type represents a struct/class, then the number of
      member functions (static and non-static) is returned. The count includes
      constructors and destructors (both explicit and implicit). Member
      functions of base classes are not included in the count.
    * Objective-C: If this type represents a struct/class, then the
      number of methods is returned. Methods in categories or super classes
      are not counted.
    "
) lldb::SBType::GetNumberOfMemberFunctions;

%feature("docstring",
    "Returns the member function of this type with the given index.

    Returns an invalid `SBTypeMemberFunction` if the index is invalid or this
    type is invalid.

    Language-specific behaviour:

    * C: Always returns an invalid `SBTypeMemberFunction`.
    * C++: Returns the member function or constructor/destructor with the given
      index.
    * Objective-C: Returns the method with the given index.

    See `GetNumberOfMemberFunctions` for what functions can be queried by this
    function.
    "
) lldb::SBType::GetMemberFunctionAtIndex;

%feature("docstring",
    "Returns true if the type is completely defined.

    Language-specific behaviour:

    * C: Returns false for struct types that were only forward declared in the
      type's `SBTarget`/`SBModule`. Otherwise returns true.
    * C++: Returns false for template/non-template struct/class types and
      scoped enums that were only forward declared inside the type's
      `SBTarget`/`SBModule`. Otherwise returns true.
    * Objective-C: Follows the same behavior as C for struct types. Objective-C
      classes are considered complete unless they were only forward declared via
      ``@class ClassName`` in the type's `SBTarget`/`SBModule`. Otherwise
      returns true.
    "
) lldb::SBType::IsTypeComplete;

%feature("docstring",
    "Returns the `TypeFlags` values for this type.

    See the respective `TypeFlags` values for what values can be set. Returns an
    integer in which each `TypeFlags` value is represented by a bit. Specific
    flags can be checked via Python's bitwise operators. For example, the
    `eTypeIsInteger` flag can be checked like this:

    ``(an_sb_type.GetTypeFlags() & lldb.eTypeIsInteger) != 0``

    If this type is invalid this returns ``0``.

    See the different values for `TypeFlags` for the language-specific meanings
    of each `TypeFlags` value.
    "
) lldb::SBType::GetTypeFlags;

%feature("docstring",
    "Searches for a directly nested type that has the provided name.

    Returns the type if it was found.
    Returns invalid type if nothing was found.
    "
) lldb::SBType::FindDirectNestedType;

%feature("docstring",
    "Writes a description of this type into the given `SBStream`.

    ``description_level`` is one of the ``lldb.eDescriptionLevel*`` enumerators
    and controls how much detail is printed. For complete types this can include
    the full layout of the type, which is useful for debugging problems with
    debug information.
    "
) lldb::SBType::GetDescription;

%feature("docstring",
"Represents a list of `SBType` objects.

Type lists are returned by the functions that can find more than one type, such
as `SBTarget.FindTypes` and `SBModule.FindTypes`.

In Python an SBTypeList supports ``len()`` and iteration::

    # A program can contain several types with the same name, e.g. one per
    # translation unit or one per module.
    for type in target.FindTypes('Task'):
        print('%s in %s' % (type.GetName(), type.GetModule().GetFileSpec().GetFilename()))

Use `SBTarget.FindFirstType` instead if only one matching type is needed.
") lldb::SBTypeList;

%feature("docstring",
"Returns whether this list was initialized.

A valid list can still be empty, use `SBTypeList.GetSize` to check for that."
) lldb::SBTypeList::IsValid;

%feature("docstring",
"Appends the given `SBType` to this list."
) lldb::SBTypeList::Append;

%feature("docstring",
"Returns the type at the given index.

Returns an invalid `SBType` if the index is out of bounds."
) lldb::SBTypeList::GetTypeAtIndex;

%feature("docstring",
"Returns the number of types in this list.

In Python this is also what ``len()`` returns."
) lldb::SBTypeList::GetSize;
