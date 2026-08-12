import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class OdrTemplateBodyMismatchTestCase(TestBase):
    def test_each_alone(self):
        """
        Each specialization can be evaluated fine on its own, as long as the
        *other* conflicting specialization hasn't already been imported into
        the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("global_box.value", result_type="int", result_value="1")

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile two genuinely ODR-violating "
        "specializations of the same class template (identical template "
        "arguments, different bodies): whichever specialization is imported "
        "into the scratch AST context first wins, and referring to the "
        "other one afterwards fails with 'undeclared identifier'"
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same template specialization
        ('Box<int>') has two incompatible definitions: one in the main
        executable and a different one (with an extra member) in a dylib.

        This differs from the 'incompatible-class-templates' test, which
        uses different template *arguments* to produce naturally distinct
        types. Here the template argument is identical, so this is a real
        ODR violation the ASTImporter has to reconcile when both
        specializations are used together in the same expression.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Using both conflicting definitions of 'Box<int>' in the same
        # expression shouldn't crash, and ideally should evaluate correctly.
        self.expect_expr(
            "global_box.value + gPluginBox->value",
            result_type="int",
            result_value="3",
        )
