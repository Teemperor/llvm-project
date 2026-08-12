import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class OdrNestedTypeMismatchTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when a class nested inside another class
        ('Outer::Inner') is defined differently in the main executable and
        in a dylib (the dylib's version has an extra member). This exercises
        ASTImporter's handling of ODR violations for types that have to be
        imported through an enclosing DeclContext rather than directly.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Using both conflicting definitions of 'Outer::Inner' in the same
        # expression shouldn't crash.
        self.expect_expr(
            "global_inner.x + gPluginInner->x",
            result_type="int",
            result_value="3",
        )
