"""
Test basic std::set functionality with import-std-module enabled.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestBasicList(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    @no_debug_info_test # remove this
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))

        self.runCmd("settings set target.import-std-module true")

        self.expect_expr("int_set.size()", result_value="3")

        # FIXME: The iterator type can't be imported to the scratch context.
        self.expect("expr (void)int_set.insert(3)")
        self.expect_expr("int_set.size()", result_value="4")

        # Redundant insert
        # FIXME: The iterator type can't be imported to the scratch context.
        self.expect("expr (void)int_set.insert(1)")
        self.expect_expr("int_set.size()", result_value="4")
        self.expect_expr("*int_set.find(5)", result_value="0")
        self.expect("expr *int_set.find(1)") #result_value="1")
        self.expect_expr("int_set.count(5)", result_value="0")
        self.expect_expr("int_set.count(1)", result_value="1")

        self.expect_expr("int_set.empty()", result_value="false")
        self.runCmd("expr int_set.clear()")
        self.expect_expr("int_set.empty()", result_value="true")


        self.expect_expr("op_member_set.size()", result_value="2")
        self.expect_expr("op_member_set.empty()", result_value="false")
        self.runCmd("expr op_member_set.clear()")
        self.expect_expr("op_member_set.empty()", result_value="true")


        self.expect_expr("op_global_set.size()", result_value="2")
        self.expect_expr("op_global_set.empty()", result_value="false")
        self.runCmd("expr op_global_set.clear()")
        self.expect_expr("op_global_set.empty()", result_value="true")
