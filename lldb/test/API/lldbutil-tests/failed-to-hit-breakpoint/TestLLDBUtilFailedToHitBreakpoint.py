"""
Tests lldbutil's behaviour when running to a source breakpoint fails.
"""

import lldb
import lldbsuite.test.lldbutil as lldbutil
from lldbsuite.test.lldbtest import *
from textwrap import dedent


class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)
    NO_DEBUG_INFO_TESTCASE = True

    def test_error_message(self):
        """
        Tests that run_to_source_breakpoint prints the right error message
        when failing to hit the wanted breakpoint.
        """
        self.build()
        try:
            lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.c"))
        except AssertionError as e:
            self.assertIn("Test process is not stopped at breakpoint, but " +
                          "instead in state 'exited'. Exit code/status: 0. " +
                          "Exit description: None.\nstderr of inferior:\n" +
                          "stderr_needle\n", str(e))
        else:
            self.fail("Hit breakpoint in unreachable code path.")
