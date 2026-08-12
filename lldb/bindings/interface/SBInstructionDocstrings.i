%feature("docstring",
"Represents a (machine language) instruction.

Instructions are produced by the disassembler, either for a whole function or
symbol (`SBFunction.GetInstructions`, `SBSymbol.GetInstructions`) or for a range
of addresses (`SBTarget.ReadInstructions`). They are handed out as part of an
`SBInstructionList`.

Note that most accessors need an `SBTarget`, because rendering an instruction
requires knowing the architecture and looking up the symbols it refers to::

    for instruction in target.ReadInstructions(frame.GetPCAddress(), 5):
        print('%s: %-8s %s' % (instruction.GetAddress(),
                               instruction.GetMnemonic(target),
                               instruction.GetOperands(target)))

See also :py:class:`SBInstructionList` and
`SBTarget.ReadInstructions`."
) lldb::SBInstruction;

%feature("docstring",
"Returns whether this object refers to an instruction."
) lldb::SBInstruction::IsValid;

%feature("docstring",
"Returns the address of this instruction as an `SBAddress`."
) lldb::SBInstruction::GetAddress;

%feature("docstring",
"Returns the mnemonic of this instruction, e.g. ``movq``.

``target`` provides the disassembler to use, whose flavor decides how the
mnemonic looks (see `SBTarget.GetInstructionsWithFlavor`)."
) lldb::SBInstruction::GetMnemonic;

%feature("docstring",
"Returns the operands of this instruction as one string, e.g. ``%rax, %rbx``.

``target`` provides the disassembler to use. See
`SBInstruction.GetMnemonic`."
) lldb::SBInstruction::GetOperands;

%feature("docstring",
"Returns the comment the disassembler produced for this instruction.

Comments hold information the disassembler could infer but that is not part of
the instruction itself, such as the symbol a branch target belongs to or the
value a PC-relative load reads. Returns an empty string if there is none."
) lldb::SBInstruction::GetComment;

%feature("docstring",
"Returns how this instruction affects control flow.

The result is one of the ``lldb.eInstructionControlFlowKind*`` enumerators,
which distinguishes for example calls, returns, conditional and unconditional
jumps from instructions that just fall through."
) lldb::SBInstruction::GetControlFlowKind;

%feature("docstring",
"Returns the opcode bytes of this instruction as an `SBData`."
) lldb::SBInstruction::GetData;

%feature("docstring",
"Returns the size of this instruction in bytes."
) lldb::SBInstruction::GetByteSize;

%feature("docstring",
"Returns whether this instruction can change the flow of control.

This is ``True`` for calls, jumps and returns. See
`SBInstruction.GetControlFlowKind` for a more precise classification."
) lldb::SBInstruction::DoesBranch;

%feature("docstring",
"Returns whether this instruction has a delay slot.

On architectures with delay slots the instruction after a branch is executed
before the branch takes effect, which matters when setting breakpoints."
) lldb::SBInstruction::HasDelaySlot;

%feature("docstring",
"Returns whether a breakpoint can be set at this instruction."
) lldb::SBInstruction::CanSetBreakpoint;

%feature("docstring",
"Prints this instruction to the given `SBFile`.

In Python a file object can be passed directly."
) lldb::SBInstruction::Print;

%feature("docstring",
"Writes a description of this instruction into the given `SBStream`."
) lldb::SBInstruction::GetDescription;

%feature("docstring",
"Emulates this instruction in the context of the given `SBFrame`.

Instruction emulation is used by LLDB itself for unwinding stacks and is mostly
interesting to test the emulation support of an architecture.
``evaluate_options`` is a bit mask of the ``lldb.eEmulateInstructionOption*``
values. Returns whether the emulation succeeded."
) lldb::SBInstruction::EmulateWithFrame;

%feature("docstring",
"Prints the emulation of this instruction for the given target triple.

Used to test the instruction emulation support of an architecture."
) lldb::SBInstruction::DumpEmulation;

%feature("docstring",
"Runs an instruction emulation test that is described in a file.

Used to test the instruction emulation support of an architecture; the results
are written into the given `SBStream`."
) lldb::SBInstruction::TestEmulation;

%feature("docstring",
"Returns the variable locations at this instruction as `SBStructuredData`.

The result is an array of dictionaries, each of which describes where a variable
lives while this instruction executes, with the keys:

* ``variable_name``: the name of the variable.
* ``location_description``: where the variable is stored (``RDI``, ``R15``,
  ``undef``, ...).
* ``start_address``: the address at which this annotation becomes valid.
* ``end_address``: the address at which this annotation becomes invalid.
* ``register_kind``: which register numbering scheme is used.
* ``decl_file``: the path of the file the variable is declared in.
* ``decl_line``: the line the variable is declared on.
* ``type_name``: the name of the variable's type."
) lldb::SBInstruction::GetVariableAnnotations;
