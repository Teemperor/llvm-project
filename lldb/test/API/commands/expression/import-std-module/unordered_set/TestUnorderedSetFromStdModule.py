"""
Test basic std::unordered_set functionality with import-std-module enabled.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestBasicList(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))

        self.runCmd("settings set target.import-std-module true")

        unordered_set_type = "std::unordered_set<int>"

        # Test an unordered_set<int, int>
        self.expect_expr("int_set", result_type=unordered_set_type)
        self.expect_expr("int_set.size()", result_value="2")
        self.expect_expr("int_set.empty()", result_value="false")
        self.runCmd("expr int_set.clear()")
        self.expect_expr("int_set.empty()", result_value="true")


        # Test a unordered_set with a custom type/hash as key.
        self.expect_expr("obj_set.size()", result_value="2")
        self.expect_expr("obj_set.empty()", result_value="false")
        self.runCmd("expr obj_set.clear()")
        self.expect_expr("obj_set.empty()", result_value="true")

