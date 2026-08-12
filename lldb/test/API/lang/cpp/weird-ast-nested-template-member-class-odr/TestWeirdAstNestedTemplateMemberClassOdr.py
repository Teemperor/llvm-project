import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstNestedTemplateMemberClassOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each global can be evaluated fine on its own, as long as the *other*
        conflicting 'Outer<int>' (and its nested 'Inner') hasn't already
        been imported into the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "main_outer.slot.a",
            result_type="int",
            result_value="2",
        )

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile two genuinely ODR-violating "
        "definitions of the same nested (non-template) member class of a "
        "class template specialization ('Outer<int>::Inner', whose "
        "DeclContext is the ClassTemplateSpecializationDecl 'Outer<int>' "
        "itself): whichever definition is imported into the scratch AST "
        "context first wins, and referring to the other one afterwards "
        "fails with 'undeclared identifier'"
    )
    def test_both_together(self):
        """
        Tests LLDB's behavior when the exact same nested class
        'Outer<int>::Inner' has two incompatible definitions: one in the
        main executable and a different one (with swapped field order) in
        a dylib.

        'Outer<int>::Inner' is a plain CXXRecordDecl whose enclosing
        DeclContext is a ClassTemplateSpecializationDecl, rather than being
        a template itself. Forcing the expression parser to resolve
        'Outer<int>::Inner' via qualified lookup, after both conflicting
        'Outer<int>' specializations have been imported into the shared
        scratch AST context, exercises an ASTImporter code path (merging a
        nested, non-templated record into an already-merged template
        specialization's DeclContext) that is less exercised than merging
        top-level ODR conflicts.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Using both conflicting definitions of 'Outer<int>::Inner' in the
        # same expression shouldn't crash, and ideally should evaluate
        # correctly.
        self.expect_expr(
            "(int)main_outer.slot.a == (int)plugin_outer.slot.a",
            result_type="bool",
            result_value="true",
        )
