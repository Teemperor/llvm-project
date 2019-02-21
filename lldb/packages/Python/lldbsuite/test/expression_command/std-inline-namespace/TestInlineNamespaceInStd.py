"""

"""

from __future__ import print_function

import lldb

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestInlineNamespaceInStd(TestBase):
    mydir = TestBase.compute_mydir(__file__)

    def setUp(self):
        TestBase.setUp(self)
        # Find the breakpoint
        self.line = line_number('main.cpp', '// lldb testsuite break')

    def test(self):
        self.build()

        self.runCmd("file "+self.getBuildArtifact("a.out"),
                    CURRENT_EXECUTABLE_SET)
        lldbutil.run_break_set_by_file_and_line(
            self,
            "main.cpp",
            self.line,
            num_expected_locations=-1,
            loc_exact=True
        )

        # If our inline namespace conversion works, we should be
        # able to only type 'std::string' instead of e.g. 'std::__1::string'.
        self.expect("expr sizeof(std::string)", substrs=['$0 = '])
