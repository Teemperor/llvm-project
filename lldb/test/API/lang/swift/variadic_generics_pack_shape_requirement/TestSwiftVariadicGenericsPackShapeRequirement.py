import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftVariadicGenericsPackShapeRequirement(TestBase):
    """Test variable inspection in a variadic generic method of a variadic
    generic type, whose generic signature has a pack shape requirement."""

    NO_DEBUG_INFO_TESTCASE = True

    @skipEmbeddedSwift
    @swiftTest
    def test(self):
        self.build()

        target, process, _, _ = lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.swift"))

        # Container(1).process(...): the pack has a single element.
        self.expect("frame variable self", substrs=["Container", "self"])

        # Container(1, "hello", 3.14).process(...): here the debug variable
        # that describes the metadata pack of the outer pack `each T` is
        # uninitialized at this PC, so the types read out of it are garbage
        # and cannot be remangled. LLDB used to build a pack type with a hole
        # in it, which tripped an assertion while remangling it. It has to
        # report an error instead, which surfaces as a diagnostic in place of
        # the value. Don't check for a specific value: if the debug info
        # improves, `self` will simply resolve to a concrete type.
        process.Continue()
        self.expect("frame variable self", substrs=["Container", "self"])

        # The debugger survived and the process wasn't killed by an assertion.
        self.assertEqual(process.GetState(), lldb.eStateStopped)
