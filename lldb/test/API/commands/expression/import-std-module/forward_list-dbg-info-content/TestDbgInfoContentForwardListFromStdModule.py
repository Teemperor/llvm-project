"""
Test std::forward_list functionality with a decl from debug info as content.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestDbgInfoContentForwardList(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))

        self.runCmd("settings set target.import-std-module true")

        self.expect("expr std::distance(a.begin(), a.end())", substrs=['(ptrdiff_t) $0 = 3'])
        self.expect("expr a.front().a", substrs=['(int) $1 = 3'])

        self.expect("expr a.begin()->a", substrs=['(int) $2 = 3'])

