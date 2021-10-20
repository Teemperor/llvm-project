import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @no_debug_info_test
    def test_call_on_base(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.cpp"))
        self.expect("expr x", substrs=["t = "])
        self.expect("expr x", substrs=["t = "])
        #self.expect_expr("x", result_type="Foo<int>", result_children=[ValueCheck()])
        #self.expect_expr("x", result_type="Foo<int>", result_children=[ValueCheck()])

