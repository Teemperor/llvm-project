"""
Test basic std::map functionality with import-std-module enabled.
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

        self.expect_expr("int_map.at(1)", result_value="2")
        self.expect_expr("int_map.at(2)", result_value="4")
        self.expect_expr("int_map[1]", result_value="2")
        self.expect_expr("int_map[2]", result_value="4")
        self.expect_expr("int_map.size()", result_value="2")
        self.expect_expr("int_map.empty()", result_value="false")
        self.runCmd("expr int_map.clear()")
        self.expect_expr("int_map.empty()", result_value="true")


        self.expect_expr("op_member_map.at(1)", result_value="2")
        self.expect_expr("op_member_map.at(2)", result_value="4")
        self.expect_expr("op_member_map[1]", result_value="2")
        self.expect_expr("op_member_map[2]", result_value="4")
        self.expect_expr("op_member_map.size()", result_value="2")
        self.expect_expr("op_member_map.empty()", result_value="false")
        self.runCmd("expr op_member_map.clear()")
        self.expect_expr("op_member_map.empty()", result_value="true")