"""
Test LLDB's behaviour when a class template is instantiated with the exact
same non-type template argument ('FixedBuf<16>') in two different modules,
but the two modules disagree about field order inside the record. Since the
non-type template argument (16) is identical, both modules'
ClassTemplateSpecializationDecls for 'FixedBuf<16>' have identical template
argument lists (and therefore identical mangled names), even though the
underlying record layouts differ - a genuine ODR violation encoded through a
non-type template parameter used as an array bound, rather than through a
differing type argument.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstNontypeTemplateParamArrayOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each module's conflicting 'FixedBuf<16>' can be evaluated fine on
        its own, as long as the *other* module's conflicting specialization
        hasn't already been imported into the shared per-target scratch AST
        context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("plugin_buf.checksum", result_type="int", result_value="222")
        self.expect_expr(
            "sizeof(plugin_buf)", result_type="unsigned long", result_value="20"
        )

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile two genuinely ODR-violating "
        "specializations of the same class template that share an identical "
        "non-type template argument (both are 'FixedBuf<16>') but disagree "
        "about field order: whichever specialization is imported into the "
        "scratch AST context first wins, and the other module's global of "
        "that same specialization degrades to '<invalid type>', with taking "
        "its address later failing to materialize ('invalid type: cannot "
        "determine size') instead of evaluating correctly"
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same template specialization
        ('FixedBuf<16>') has two incompatible field layouts: one in the main
        executable ('data' then 'checksum') and a different one (swapped
        order: 'checksum' then 'data') in a dylib. Both specializations
        share the identical non-type template argument (16), so this is a
        real ODR violation the ASTImporter has to reconcile when both
        specializations are used together in the same expression.

        Using both conflicting definitions of 'FixedBuf<16>' in the same
        expression shouldn't crash, and ideally should evaluate correctly.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_buf.checksum", result_type="int", result_value="111")

        self.expect_expr(
            "main_buf.checksum + plugin_buf.checksum",
            result_type="int",
            result_value="333",
        )

        self.expect_expr(
            "sizeof(main_buf) + sizeof(plugin_buf)",
            result_type="unsigned long",
            result_value="40",
        )
