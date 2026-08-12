%feature("docstring",
"Represents a list of machine instructions.

Instruction lists are returned by the disassembling functions:
`SBFunction.GetInstructions`, `SBSymbol.GetInstructions`,
`SBTarget.ReadInstructions` and `SBTarget.GetInstructions`.

SBInstructionList supports instruction (:py:class:`SBInstruction` instance) iteration.
For example (see also :py:class:`SBDebugger` for a more complete example), ::

    def disassemble_instructions(insts):
        for i in insts:
            print(i)

defines a function which takes an SBInstructionList instance and prints out
the machine instructions in assembly format.

In Python the list also supports ``len()`` and indexing::

    instructions = function.GetInstructions(target)
    print('%d instructions, first one is %s' % (len(instructions),
                                                instructions[0].GetMnemonic(target)))
"
) lldb::SBInstructionList;

%feature("docstring",
"Returns whether this object holds a list of instructions."
) lldb::SBInstructionList::IsValid;

%feature("docstring",
"Returns the number of instructions in this list.

In Python this is also what ``len()`` returns."
) lldb::SBInstructionList::GetSize;

%feature("docstring",
"Returns the instruction at the given index as an `SBInstruction`."
) lldb::SBInstructionList::GetInstructionAtIndex;

%feature("docstring",
"Returns how many instructions of this list are in the given address range.

``start`` and ``end`` are `SBAddress` objects. If ``canSetBreakpoint`` is
``True``, only instructions a breakpoint can be set on are counted."
) lldb::SBInstructionList::GetInstructionsCount;

%feature("docstring",
"Removes all instructions from this list."
) lldb::SBInstructionList::Clear;

%feature("docstring",
"Appends an `SBInstruction` to this list."
) lldb::SBInstructionList::AppendInstruction;

%feature("docstring",
"Prints all instructions of this list to the given `SBFile`.

In Python a file object can be passed directly."
) lldb::SBInstructionList::Print;

%feature("docstring",
"Writes a description of all instructions in this list into the given
`SBStream`."
) lldb::SBInstructionList::GetDescription;

%feature("docstring",
"Prints the emulation of all instructions of this list for the given target
triple.

Used to test the instruction emulation support of an architecture, see
`SBInstruction.DumpEmulation`."
) lldb::SBInstructionList::DumpEmulationForAllInstructions;
