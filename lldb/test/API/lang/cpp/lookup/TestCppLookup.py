import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,"// break in ns", lldb.SBFileSpec("main.cpp"))
        self.expect_expr("global_func()", result_type="int", result_value="1")
        self.expect_expr("ns_func()", result_type="int", result_value="2")
        self.expect_expr("nested_func()", error_msg="foo")

        lldbutil.run_to_source_breakpoint(self,"// break in ns::nested", lldb.SBFileSpec("main.cpp"))
        self.expect_expr("global_func()", result_type="int", result_value="1")
        self.expect_expr("ns_func()", result_type="int", result_value="2")
        self.expect_expr("nested_func()", result_type="int", result_value="3")

        lldbutil.run_to_source_breakpoint(self,"// break in main", lldb.SBFileSpec("main.cpp"))
        self.expect_expr("global_func()", result_type="int", result_value="1")
        self.expect_expr("nested_func()", error_msg="asdf")
        self.expect_expr("ns_func()", error_msg="asdf")
