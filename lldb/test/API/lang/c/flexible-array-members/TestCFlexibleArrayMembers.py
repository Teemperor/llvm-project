"""
Tests C99's flexible array members.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @no_debug_info_test
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.c"))

        self.expect_expr("s->flexible", result_type="char *", result_summary='"contents"')
        self.expect_var_path("s->flexible", type="char[]", summary='"contents"')
