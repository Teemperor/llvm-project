import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class ExprXValuePrintingTestCase(TestBase):
    def test(self):
        """Printing an xvalue should work."""
        self.build_and_run()
        self.expect_expr("foo().data", result_value="1234")
