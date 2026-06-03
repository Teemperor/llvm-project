"""
Test that lldb displays variables of all floating point types correctly in C.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        """Check the types and values of all float-typed variables."""
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.c")
        )

        # Check every scalar floating point type both via 'frame variable' (var
        # path) and via the expression evaluator.
        self.expect_var_path("the_float", type="float", value="3.5")
        self.expect_expr("the_float", result_type="float", result_value="3.5")

        self.expect_var_path("the_double", type="double", value="6.25")
        self.expect_expr("the_double", result_type="double", result_value="6.25")

        self.expect_var_path("the_long_double", type="long double", value="10.75")
        self.expect_expr(
            "the_long_double", result_type="long double", result_value="10.75"
        )

        # Check the different "shapes" of the double type: array, pointer, struct
        # and union.
        self.expect_var_path("the_double_array", type="double[3]")
        self.expect_var_path("the_double_array[0]", type="double", value="1.5")
        self.expect_var_path("the_double_array[1]", type="double", value="2.5")
        self.expect_var_path("the_double_array[2]", type="double", value="3.5")

        self.expect_var_path("*the_double_ptr", type="double", value="6.25")
        self.expect_expr("*the_double_ptr", result_type="double", result_value="6.25")

        self.expect_var_path("the_float_struct.a", type="float", value="0.5")
        self.expect_var_path("the_float_struct.b", type="double", value="0.25")
        self.expect_expr("the_float_struct.a", result_type="float", result_value="0.5")

        self.expect_var_path("the_float_union.as_double", type="double", value="0.125")
        self.expect_expr(
            "the_float_union.as_double", result_type="double", result_value="0.125"
        )

        # Check edge-case values: zero, -1 and a negative value.
        self.expect_var_path("float_zero", type="float", value="0")
        self.expect_var_path("float_neg_one", type="float", value="-1")
        self.expect_var_path("float_neg", type="float", value="-2.5")
        self.expect_var_path("double_zero", type="double", value="0")
        self.expect_var_path("double_neg_one", type="double", value="-1")
        self.expect_var_path("double_neg", type="double", value="-2.5")
        self.expect_var_path("long_double_zero", type="long double", value="0")
        self.expect_var_path("long_double_neg_one", type="long double", value="-1")

        self.expect_expr("float_neg", result_type="float", result_value="-2.5")
        self.expect_expr("double_zero", result_type="double", result_value="0")
        self.expect_expr(
            "long_double_neg_one", result_type="long double", result_value="-1"
        )
