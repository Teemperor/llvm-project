"""
Test basic std::shared_ptr functionality.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestSharedPtr(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
                                          "// Set break point at this line.",
                                          lldb.SBFileSpec("main.cpp"))

        self.runCmd("settings set target.import-std-module true")
        self.runCmd("log enable lldb expr -f ~/expr.log")

        set_type = "std::set<int, std::less<int>, std::allocator<int> >"
        size_type = set_type + "::size_type"

        self.expect_expr("int_set",
                         result_type=set_type,
                         result_children=[
            ValueCheck(value="1"),
            ValueCheck(value="2"),
            ValueCheck(value="3"),
            ValueCheck(value="4"),
            ValueCheck(value="5")
        ])
        self.expect_expr("int_set.size()", result_type=size_type, result_value="5")
        self.expect_expr("int_set.count(1)", result_type=size_type, result_value="1")

