"""
Test that types nested in a class don't leak into the global scope of an
expression: after the expression evaluator has seen `ns::Outer::Inner`, an
unqualified `Inner` must still refer to the global `Inner`.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestNestedTypeShadowsGlobal(TestBase):
    @add_test_categories(["typesystem-clike"])
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("nested", result_type="ns::Outer::Inner")
        self.expect_expr("nested_enum", result_type="ns::Outer::NestedEnum")

        # Refer to the nested types (through the variables) and then to the
        # global types with the same unqualified names in one expression.
        self.expect_expr(
            "(void)nested; Inner tmp = global_inner; tmp.b",
            result_type="double",
            result_value="3.5",
        )
        self.expect_expr(
            "(void)nested; sizeof(Inner) == sizeof(global_inner)",
            result_type="bool",
            result_value="true",
        )
        self.expect_expr(
            "(void)nested_enum; NestedEnum e = global_enum; e",
            result_type="NestedEnum",
            result_value="GlobalValue",
        )

        # The nested types are still found by their qualified names.
        self.expect_expr(
            "(void)nested; ns::Outer::Inner copy = nested; copy.x",
            result_type="int",
            result_value="42",
        )
        self.expect_expr(
            "ns::Outer::NestedEnum::NestedValue",
            result_type="ns::Outer::NestedEnum",
            result_value="NestedValue",
        )
