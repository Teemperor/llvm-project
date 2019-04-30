"""
Test std::map functionality.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestBasicVector(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    # FIXME: This should work on more setups, so remove these
    # skipIf's in the future.
    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    @skipIf(oslist=no_match(["linux"]))
    @skipIf(debug_info=no_match(["dwarf"]))
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))

        self.runCmd("settings set target.import-std-module true")

        self.expect("expr (size_t)m.size()", substrs=['(size_t) $0 = 2'])
        self.expect("expr m[C(3)] = C(-3)", substrs=['(C) $1 = (i = -3)'])
        self.expect("expr m.at(C(3))", substrs=['(C) $2 = (i = -3)'])
        self.expect("expr (size_t)m.size()", substrs=['(size_t) $3 = 3'])
