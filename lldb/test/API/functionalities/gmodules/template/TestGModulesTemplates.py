import lldb
import os
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestWithGmodulesDebugInfo(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["gmodules"])
    def test_specialized_typedef_from_pch(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.cpp"))

        # Member access
        self.expect_expr("C.Base1::m_base", result_type="int", result_value="11")
