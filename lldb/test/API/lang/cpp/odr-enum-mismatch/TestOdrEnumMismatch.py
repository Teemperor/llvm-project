import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class OdrEnumMismatchTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when an enum with the same name and the same
        enumerators ('Color') is defined with different underlying values
        in the main executable (Red=0, Green=1, Blue=2) and in a dylib
        (Red=10, Green=11, Blue=12). This is an ODR violation and forces the
        ASTImporter to reconcile two conflicting EnumDecls for 'Color' when
        both are used together in the same expression.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("global_color", result_type="Color", result_value="Green")
        self.expect_expr("*gPluginColor", result_type="Color", result_value="Green")

        # Using both conflicting definitions of 'Color' in the same
        # expression shouldn't crash and each should keep its own value.
        self.expect_expr(
            "(int)global_color + (int)*gPluginColor",
            result_type="int",
            result_value="12",
        )
