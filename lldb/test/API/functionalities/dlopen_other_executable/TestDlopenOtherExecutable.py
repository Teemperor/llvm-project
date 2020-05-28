import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @skipIfRemote
    @skipIfWindows
    @no_debug_info_test
    def test(self):
        self.build()
        # Launch and stop before the dlopen call.
        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.c"))
        # Check that the executable is the test binary.
        self.assertEqual(self.target().GetExecutable().GetFilename(), "a.out")
        # Continue so that dlopen is called.
        self.process().Continue()
        # Check that the executable is still the test binary and not "other".
        self.assertEqual(self.target().GetExecutable().GetFilename(), "a.out")
