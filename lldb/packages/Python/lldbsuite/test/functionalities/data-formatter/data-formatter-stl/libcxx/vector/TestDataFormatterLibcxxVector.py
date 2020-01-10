"""
Test lldb data formatter subsystem.
"""



import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class LibcxxVectorDataFormatterTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["libc++"])
    @skipIf(debug_info=no_match(["gmodules"]))
    def test_with_run_command(self):
        """Test that that file and class static variables display correctly."""
        self.build()
        (self.target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.cpp", False))

        # check if we can display strings
        self.expect("frame variable strings",
                    substrs=['goofy',
                             'is',
                             'smart'])

        self.expect("p strings",
                    substrs=['goofy',
                             'is',
                             'smart_error'])
