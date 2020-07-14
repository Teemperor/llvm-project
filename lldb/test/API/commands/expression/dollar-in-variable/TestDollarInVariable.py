import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @expectedFailureAll(oslist=["windows"])
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.c"))

        self.expect_expr("$__lldb_expr_result")
        self.expect_expr("$foo", result_type="int", result_value="12")
        self.expect_expr("$R0", result_type="int", result_value="13")
        self.expect_expr("int $foo = 123; $foo", result_type="int", result_value="123")
        self.expect_expr("$0", result_type="int &", result_value="11")
