%feature(
    "docstring",
    "Represents a member of an enum in lldb.

An enum member has a name and a value. The members of an enumeration type are
returned by `SBType.GetEnumMembers`::

    for member in my_enum_type.GetEnumMembers():
        print('%s = %d' % (member.GetName(), member.GetValueAsSigned()))

See also :py:class:`SBTypeEnumMemberList` and :py:class:`SBType`."
) lldb::SBTypeEnumMember;

%feature("docstring",
"Returns whether this object refers to an enum member."
) lldb::SBTypeEnumMember::IsValid;

%feature("docstring",
"Returns the value of this enum member as a signed integer.

Use `SBTypeEnumMember.GetValueAsUnsigned` for enumerations with an unsigned
underlying type, see `SBType.GetEnumerationIntegerType`."
) lldb::SBTypeEnumMember::GetValueAsSigned;

%feature("docstring",
"Returns the value of this enum member as an unsigned integer."
) lldb::SBTypeEnumMember::GetValueAsUnsigned;

%feature("docstring",
"Returns the name of this enum member."
) lldb::SBTypeEnumMember::GetName;

%feature("docstring",
"Returns the enumeration type this member belongs to as an `SBType`."
) lldb::SBTypeEnumMember::GetType;

%feature("docstring",
"Writes a description of this enum member into the given `SBStream`."
) lldb::SBTypeEnumMember::GetDescription;

%feature("docstring",
"Represents a list of SBTypeEnumMembers.

SBTypeEnumMemberList supports SBTypeEnumMember iteration.
It also supports [] access either by index, or by enum
element name by doing: ::

  myType = target.FindFirstType('MyEnumWithElementA')
  members = myType.GetEnumMembers()
  first_elem = members[0]
  elem_A = members['A']

Enum member lists are returned by `SBType.GetEnumMembers`.
") lldb::SBTypeEnumMemberList;

%feature("docstring",
"Returns whether this object holds a list of enum members."
) lldb::SBTypeEnumMemberList::IsValid;

%feature("docstring",
"Appends an `SBTypeEnumMember` to this list."
) lldb::SBTypeEnumMemberList::Append;

%feature("docstring",
"Returns the enum member at the given index as an `SBTypeEnumMember`."
) lldb::SBTypeEnumMemberList::GetTypeEnumMemberAtIndex;

%feature("docstring",
"Returns the number of enum members in this list.

In Python this is also what ``len()`` returns."
) lldb::SBTypeEnumMemberList::GetSize;
