"""
Test LLDB's behaviour when the exact same class template specialization
type-id ('Holder<int>') is an ordinary implicit instantiation of a primary
template in one module (the main executable) and an explicit, differently
shaped full specialization in another module (a dylib).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTemplateExplicitSpecializationOdrTestCase(TestBase):
    def test_holder_alone_in_main_exe(self):
        """
        Looking at the main executable's 'Holder<int>' (an implicit
        instantiation of the primary template, with a single 'v' member)
        on its own should work fine, before the dylib's conflicting
        explicit specialization of the same template-id is ever imported
        into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_holder.v", result_type="int", result_value="11")

    def test_holder_implicit_vs_explicit_specialization_across_modules(self):
        """
        Tests LLDB's behaviour when the same template-id 'Holder<int>' is:
          - an implicit instantiation of the primary template
            'template<typename T> struct Holder { T v; };' in the main
            executable (a ClassTemplateSpecializationDecl with
            TSK_ImplicitInstantiation), and
          - an explicit full specialization
            'template<> struct Holder<int> { int v; int extra; long tag; };'
            in a dylib (a ClassTemplateSpecializationDecl with
            TSK_ExplicitSpecialization), with a completely different body
            and size.

        Evaluating expressions that reference both modules' globals of
        type 'Holder<int>' forces LLDB's expression evaluator to import
        and reconcile both AST nodes for 'Holder<int>' within the same
        target AST context. Since Clang represents implicit
        instantiations and explicit specializations very differently
        (different TemplateSpecializationKind, different provenance for
        the members), this is a mismatch the ASTImporter's structural
        equivalence / ODR checking may not expect - it is built around
        two conflicting *implicit* instantiations, not one side being an
        explicit specialization that was never instantiated from the
        primary template pattern at all. This is a real ODR violation:
        the same specialization has two incompatible definitions across
        translation units, and the hope here is that LLDB either crashes
        or reports a coherent diagnostic instead of silently corrupting
        the AST or the type layout it hands back to the expression JIT.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # On its own, this should not crash LLDB even though 'Holder<int>'
        # here is an explicit specialization with a different body than
        # what the main executable's implicit instantiation would predict.
        self.expect_expr("plugin_holder.v", result_type="int", result_value="22")
        self.expect_expr("plugin_holder.extra", result_type="int", result_value="33")
        self.expect_expr("plugin_holder.tag", result_type="long", result_value="44")

        # Force both conflicting 'Holder<int>' definitions to be imported
        # and reconciled within a single expression's AST context.
        self.expect_expr("main_holder.v", result_type="int", result_value="11")

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile an implicit instantiation of a "
        "class template in one module against an explicit full specialization "
        "of the same template-id in another module: whichever "
        "ClassTemplateSpecializationDecl is imported into the scratch AST "
        "context first wins, and referring to the other module's global of "
        "that same specialization afterwards fails with 'undeclared identifier'"
    )
    def test_holder_implicit_vs_explicit_specialization_across_modules_combined_expr(
        self,
    ):
        """
        Combine globals from both modules in one expression: this forces
        the ASTImporter to line up the main executable's implicit
        instantiation of 'Holder<int>' against the dylib's explicit
        specialization of the same template-id while evaluating a single
        expression.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "main_holder.v + plugin_holder.v",
            result_type="int",
            result_value="33",
        )
