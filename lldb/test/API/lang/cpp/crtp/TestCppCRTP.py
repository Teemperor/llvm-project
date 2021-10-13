"""
A test for the curiously recurring template pattern (or CRTP).

Note that the derived class is referenced directly from the parent class in the
test. If this fails then there is a good chance that LLDB tried to eagerly
resolve the derived class while constructing the base class.
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
        self.createTestTarget()

        # Try using the class in the expression evaluator.
        self.expect_expr("x", result_type="X", result_children=[
            ValueCheck(name="Base<X>"),
            ValueCheck(name="member", value="0")
        ])

        # Try accessing the members of the class and base class.
        self.expect_expr("x.pointer", result_type="X *")
        self.expect_expr("x.member", result_type="int", result_value="0")
