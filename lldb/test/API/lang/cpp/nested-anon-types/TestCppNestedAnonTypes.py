import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.cpp"))

        hex_12345678 = "305419896"
        self.expect_expr("s.UI.u", result_type="unsigned int", result_value=hex_12345678)
        self.expect_expr("s.SI.i", result_type="int", result_value=hex_12345678)
