import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstSelfIncludeDoubleGuardOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        This dylib contains two compile units (unit1.cpp and unit2.cpp)
        that each define their own 'struct Point', but with conflicting
        field types ('int' vs. 'long'). This is a real ODR violation
        entirely within a single binary's own DWARF: no cross-module
        import is involved at all, unlike most of the other 'weird-ast'
        ODR tests in this directory.

        Referring to just one compile unit's 'Point' at a time (without
        ever using the other one in the same expression) works fine and
        returns the expected, well-typed result.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("f1()->x", result_type="int", result_value="1")
        self.expect_expr("f2()->x", result_type="long", result_value="10")

    def test_both_together(self):
        """
        Using both conflicting definitions of 'Point' together in the same
        expression should not crash LLDB. Since both compile units'
        'Point' share the same qualified name, the expression evaluator
        picks one of the two (real, on-disk) DWARF definitions of 'Point'
        for the whole expression, so the result ends up well-typed (here
        as 'long', matching unit2.cpp's definition) even though the value
        itself is still numerically correct for both sides (1 + 10 == 11).
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("f1()->x + f2()->x", result_type="long", result_value="11")
