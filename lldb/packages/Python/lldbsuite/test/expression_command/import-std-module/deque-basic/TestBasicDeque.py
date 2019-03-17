"""
Test basic std::list functionality.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestBasicDeque(TestBase):

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

        self.expect("expr a.size()", substrs=['(size_t) $0 = 3'])
        self.expect("expr a.front()", substrs=['(int) $1 = 3'])
        self.expect("expr a.back()", substrs=['(int) $2 = 2'])

        self.expect("expr std::sort(a.begin(), a.end())")
        self.expect("expr a.front()", substrs=['(int) $3 = 1'])
        self.expect("expr a.back()", substrs=['(int) $4 = 3'])

        self.expect("expr a.front()", substrs=['(int) $5 = 1'])
        self.expect("expr a.back()", substrs=['(int) $6 = 3'])

        # FIXME: We shouldn't need to cast the result, but it seems LLDB can't
        # deduce the result type at the moment.
        self.expect("expr (int)(*a.begin())", substrs=['(int) $7 = 1'])
        self.expect("expr (int)(*a.rbegin())", substrs=['(int) $8 = 3'])

