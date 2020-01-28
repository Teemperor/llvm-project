"""
Tests std::queue functionality.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))

        self.runCmd("settings set target.import-std-module true")

        self.expect_expr("(int)map_with_dbg_value.size()", result_value="1")
        self.expect_expr("map_with_dbg_value[0].i = 0", result_type="int", result_value="0")

        self.expect_expr("(int)map_with_dbg_key.size()", result_value="1")
        self.expect_expr("(int)(map_with_dbg_key[key1] = 0)", result_type="int", result_value="0")
