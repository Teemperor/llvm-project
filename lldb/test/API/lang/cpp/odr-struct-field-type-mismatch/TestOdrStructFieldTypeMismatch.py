import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class OdrStructFieldTypeMismatchTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when a struct with the same name ('Point') is
        defined with a field of a different type in the main executable
        (int) and in a dylib (float). This is an ODR violation and forces
        the ASTImporter to reconcile two conflicting RecordDecls for
        'Point' when both are pulled into the same scratch AST context.

        The two definitions are never used together in the same expression
        below (each is only ever imported on its own), which is the common
        case in real-world code (e.g. two libraries that happen to reuse a
        type name for unrelated purposes). This should work without
        crashing or misinterpreting one type as the other.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "global_point",
            result_type="Point",
            result_children=[ValueCheck(name="x", value="1")],
        )
        self.expect_expr(
            "*gPluginPoint",
            result_type="Point",
            result_children=[ValueCheck(name="x", value="2.5")],
        )
