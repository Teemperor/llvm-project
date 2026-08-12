import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class OdrClassDifferentBaseTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when a class with the same name ('Shape') is
        defined with different base classes in the main executable
        (BaseA) and in a dylib (BaseB). This is an ODR violation and the
        two conflicting CXXRecordDecls end up needing to be reconciled by
        the ASTImporter when both are used together in the same expression.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Using both conflicting definitions of 'Shape' in the same
        # expression shouldn't crash.
        self.expect_expr(
            "global_shape.shapeField + gPluginShape->shapeField",
            result_type="int",
            result_value="22",
        )
