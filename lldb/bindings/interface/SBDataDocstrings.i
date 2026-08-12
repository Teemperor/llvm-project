%feature("docstring",
"Represents a read-only buffer of target memory with a known byte order.

An SBData is a bag of bytes plus the two pieces of information that are needed
to interpret those bytes: the byte order (endianness) of the target that
produced them and the size of an address on that target. Reading a value out of
an SBData therefore does not require a live process; the data has already been
copied out of the target.

Instances are usually obtained from another object rather than created
directly:

* `SBValue.GetData` returns the bytes that make up a variable.
* `SBValue.GetPointeeData` returns the bytes a pointer points to.
* `SBSection.GetSectionData` returns the contents of a section of a module.
* `SBInstruction.GetData` returns the opcode bytes of an instruction.

The ``CreateDataFrom*`` class methods and the ``SetDataFrom*`` methods can be
used to build a buffer from scratch, which is mostly useful together with
`SBTarget.CreateValueFromData` to give some raw bytes a type and inspect them
like a variable.

All read accessors take a byte offset into the buffer and an `SBError` that is
set if the requested bytes are outside of the buffer. For example,::

    # Read the bytes of the local variable 'x' and interpret the first four
    # of them as an unsigned integer.
    data = frame.FindVariable('x').GetData()
    error = lldb.SBError()
    value = data.GetUnsignedInt32(error, 0)
    if error.Fail():
        print('failed to read: %s' % error.GetCString())

The Python bindings add a few conveniences on top of the C++ API:

* ``len(data)`` is the same as `SBData.GetByteSize`.
* The ``uint8``, ``uint16``, ``uint32``, ``uint64``, ``sint8``, ``sint16``,
  ``sint32``, ``sint64``, ``float`` and ``double`` properties return an
  array-like object that is indexed in units of the respective type instead of
  in bytes, so ``data.uint32[2]`` reads the third 32-bit integer in the buffer
  and ``data.uint32[0:2]`` returns a list with the first two.
* The plural properties (``uint8s``, ``uint32s``, ``doubles``, ...) return a
  list with the whole buffer decoded as that type.
* ``SBData.CreateDataFromInt(value, size=None, target=None, ptr_size=None,
  endian=None)`` creates a buffer holding a single integer and picks a fitting
  size, byte order and address size if they are not given explicitly.

For example,::

    # Print all pointers in a section, assuming a 64-bit target.
    for pointer in section.GetSectionData().uint64s:
        print(hex(pointer))
"
) lldb::SBData;

%feature("docstring",
"Returns the number of bytes in this buffer.

Returns ``0`` for an invalid SBData. In Python this is also what ``len()``
returns for an SBData."
) lldb::SBData::GetByteSize;

%feature("docstring",
"Returns the size in bytes of an address in the target that produced this data.

This is the size that `SBData.GetAddress` uses when reading an address out of
the buffer. It is typically ``4`` or ``8``."
) lldb::SBData::GetAddressByteSize;

%feature("docstring",
"Sets the size in bytes of an address in the target that produced this data.

This only changes how `SBData.GetAddress` interprets the bytes in the buffer,
it does not modify the data itself."
) lldb::SBData::SetAddressByteSize;

%feature("docstring",
"Returns the byte order of the data as an ``lldb.eByteOrder*`` enumerator.

All multi-byte reads such as `SBData.GetUnsignedInt32` decode the bytes using
this byte order."
) lldb::SBData::GetByteOrder;

%feature("docstring",
"Sets the byte order used when decoding values out of this buffer.

Takes one of the ``lldb.eByteOrder*`` enumerators. This only changes how the
bytes are interpreted, it does not swap the bytes in the buffer::

    data.SetByteOrder(lldb.eByteOrderBig)
"
) lldb::SBData::SetByteOrder;

%feature("docstring",
"Empties this buffer.

After this call `SBData.GetByteSize` returns ``0`` and the object is invalid."
) lldb::SBData::Clear;

%feature("docstring",
"Reads a 4-byte floating point value at the given byte offset.

``error`` is set and ``0`` is returned if there are not enough bytes left in
the buffer at ``offset``."
) lldb::SBData::GetFloat;

%feature("docstring",
"Reads an 8-byte floating point value at the given byte offset.

``error`` is set and ``0`` is returned if there are not enough bytes left in
the buffer at ``offset``."
) lldb::SBData::GetDouble;

%feature("docstring",
"Reads a target ``long double`` value at the given byte offset.

The size of a ``long double`` depends on the target architecture. ``error`` is
set and ``0`` is returned if there are not enough bytes left in the buffer at
``offset``."
) lldb::SBData::GetLongDouble;

%feature("docstring",
"Reads an address at the given byte offset.

The number of bytes that are read is `SBData.GetAddressByteSize`. ``error`` is
set and ``0`` is returned if there are not enough bytes left in the buffer at
``offset``.

The returned value is a plain integer. Use `SBTarget.ResolveLoadAddress` to
turn it into an `SBAddress` that can be resolved to a symbol or line entry."
) lldb::SBData::GetAddress;

%feature("docstring",
"Reads an unsigned 8-bit integer at the given byte offset.

``error`` is set and ``0`` is returned if the offset is outside of the buffer."
) lldb::SBData::GetUnsignedInt8;

%feature("docstring",
"Reads an unsigned 16-bit integer at the given byte offset.

The bytes are decoded using `SBData.GetByteOrder`. ``error`` is set and ``0``
is returned if there are not enough bytes left in the buffer at ``offset``."
) lldb::SBData::GetUnsignedInt16;

%feature("docstring",
"Reads an unsigned 32-bit integer at the given byte offset.

The bytes are decoded using `SBData.GetByteOrder`. ``error`` is set and ``0``
is returned if there are not enough bytes left in the buffer at ``offset``."
) lldb::SBData::GetUnsignedInt32;

%feature("docstring",
"Reads an unsigned 64-bit integer at the given byte offset.

The bytes are decoded using `SBData.GetByteOrder`. ``error`` is set and ``0``
is returned if there are not enough bytes left in the buffer at ``offset``."
) lldb::SBData::GetUnsignedInt64;

%feature("docstring",
"Reads a signed 8-bit integer at the given byte offset.

``error`` is set and ``0`` is returned if the offset is outside of the buffer."
) lldb::SBData::GetSignedInt8;

%feature("docstring",
"Reads a signed 16-bit integer at the given byte offset.

The bytes are decoded using `SBData.GetByteOrder`. ``error`` is set and ``0``
is returned if there are not enough bytes left in the buffer at ``offset``."
) lldb::SBData::GetSignedInt16;

%feature("docstring",
"Reads a signed 32-bit integer at the given byte offset.

The bytes are decoded using `SBData.GetByteOrder`. ``error`` is set and ``0``
is returned if there are not enough bytes left in the buffer at ``offset``."
) lldb::SBData::GetSignedInt32;

%feature("docstring",
"Reads a signed 64-bit integer at the given byte offset.

The bytes are decoded using `SBData.GetByteOrder`. ``error`` is set and ``0``
is returned if there are not enough bytes left in the buffer at ``offset``."
) lldb::SBData::GetSignedInt64;

%feature("docstring",
"Reads a null-terminated C string starting at the given byte offset.

Returns the string without the terminating null byte. ``error`` is set and
``None`` is returned if there is no null byte in the remainder of the buffer or
if the offset is outside of the buffer."
) lldb::SBData::GetString;

%feature("docstring",
"Reads ``size`` raw bytes starting at the given byte offset.

In Python the ``buf`` parameter of the C++ API is omitted and the bytes that
were read are returned as a ``bytes`` object::

    first_16_bytes = data.ReadRawData(lldb.SBError(), 0, 16)

``error`` is set if fewer than ``size`` bytes are available at ``offset``."
) lldb::SBData::ReadRawData;

%feature("docstring",
"Writes a textual representation of this buffer to the given `SBStream`.

``base_addr`` is only used for the address column of the output and does not
influence how the data is read."
) lldb::SBData::GetDescription;

%feature("docstring",
"Replaces the contents of this buffer with the given bytes.

In Python the ``buf`` and ``size`` parameters of the C++ API are replaced by a
single ``bytes``-like object::

    data = lldb.SBData()
    data.SetData(lldb.SBError(), b'\\x01\\x00\\x00\\x00', lldb.eByteOrderLittle, 8)
    print(data.GetUnsignedInt32(lldb.SBError(), 0))  # prints 1

``endian`` is one of the ``lldb.eByteOrder*`` enumerators and ``addr_size`` is
the size in bytes of an address in the target the data belongs to.

The buffer is not copied, so it has to stay alive for as long as this SBData is
used. Use `SBData.SetDataWithOwnership` to have LLDB take a copy instead."
) lldb::SBData::SetData;

%feature("docstring",
"Replaces the contents of this buffer with a copy of the given bytes.

Behaves like `SBData.SetData` except that LLDB copies the bytes, so the caller
does not have to keep the input buffer alive."
) lldb::SBData::SetDataWithOwnership;

%feature("docstring",
"Appends the contents of another SBData to this one.

Returns ``False`` if either of the two objects is invalid. The byte order and
address size of this object are kept, so appending data from a differently
sized or ordered target will produce values that cannot be decoded correctly."
) lldb::SBData::Append;

%feature("docstring",
"Creates an SBData holding the given string including its terminating null byte.

This is a class method::

    data = lldb.SBData.CreateDataFromCString(lldb.eByteOrderLittle, 8, 'hello')
"
) lldb::SBData::CreateDataFromCString;

%feature("docstring",
"Creates an SBData from a list of unsigned 64-bit integers.

This is a class method. ``endian`` is one of the ``lldb.eByteOrder*``
enumerators and ``addr_byte_size`` is the size in bytes of an address in the
target the data is meant for::

    data = lldb.SBData.CreateDataFromUInt64Array(lldb.eByteOrderLittle, 8, [1, 2, 3])
"
) lldb::SBData::CreateDataFromUInt64Array;

%feature("docstring",
"Creates an SBData from a list of unsigned 32-bit integers.

See `SBData.CreateDataFromUInt64Array` for a description of the parameters."
) lldb::SBData::CreateDataFromUInt32Array;

%feature("docstring",
"Creates an SBData from a list of signed 64-bit integers.

See `SBData.CreateDataFromUInt64Array` for a description of the parameters."
) lldb::SBData::CreateDataFromSInt64Array;

%feature("docstring",
"Creates an SBData from a list of signed 32-bit integers.

See `SBData.CreateDataFromUInt64Array` for a description of the parameters."
) lldb::SBData::CreateDataFromSInt32Array;

%feature("docstring",
"Creates an SBData from a list of double precision floating point values.

See `SBData.CreateDataFromUInt64Array` for a description of the parameters."
) lldb::SBData::CreateDataFromDoubleArray;

%feature("docstring",
"Replaces the contents of this buffer with the given string and its null byte.

The byte order and address size of this object are left unchanged. Returns
``False`` on failure."
) lldb::SBData::SetDataFromCString;

%feature("docstring",
"Replaces the contents of this buffer with a list of unsigned 64-bit integers.

The values are encoded using the byte order of this object. Returns ``False``
on failure."
) lldb::SBData::SetDataFromUInt64Array;

%feature("docstring",
"Replaces the contents of this buffer with a list of unsigned 32-bit integers.

The values are encoded using the byte order of this object. Returns ``False``
on failure."
) lldb::SBData::SetDataFromUInt32Array;

%feature("docstring",
"Replaces the contents of this buffer with a list of signed 64-bit integers.

The values are encoded using the byte order of this object. Returns ``False``
on failure."
) lldb::SBData::SetDataFromSInt64Array;

%feature("docstring",
"Replaces the contents of this buffer with a list of signed 32-bit integers.

The values are encoded using the byte order of this object. Returns ``False``
on failure."
) lldb::SBData::SetDataFromSInt32Array;

%feature("docstring",
"Replaces the contents of this buffer with a list of double precision values.

The values are encoded using the byte order of this object. Returns ``False``
on failure."
) lldb::SBData::SetDataFromDoubleArray;
