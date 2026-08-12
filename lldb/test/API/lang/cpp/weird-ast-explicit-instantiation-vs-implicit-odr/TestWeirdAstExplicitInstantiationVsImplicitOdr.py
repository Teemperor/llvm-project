"""
Test LLDB's behaviour when the exact same class template specialization
type-id ('Stat<double>') is an *explicit instantiation definition* in one
module (the main executable) and an ordinary *implicit instantiation* of a
differently laid out primary template in another module (a dylib).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstExplicitInstantiationVsImplicitOdrTestCase(TestBase):
    def test_stat_alone_in_main_exe(self):
        """
        Looking at the main executable's 'Stat<double>' (an explicit
        instantiation definition, with members 'sum' then 'count') on its
        own should work fine, before the dylib's conflicting implicit
        instantiation of the same template-id is ever imported into the
        scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_stat.sum", result_type="double", result_value="1")
        self.expect_expr("main_stat.count", result_type="double", result_value="2")

    def test_stat_alone_in_dylib(self):
        """
        Looking at the dylib's 'Stat<double>' (an implicit instantiation,
        with members 'count' then 'sum' - the reverse order from the main
        executable) on its own should also work fine, before the main
        executable's conflicting explicit instantiation definition of the
        same template-id is ever imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("plugin_stat.count", result_type="double", result_value="3")
        self.expect_expr("plugin_stat.sum", result_type="double", result_value="4")

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile an explicit instantiation "
        "definition of a class template in one module against an implicit "
        "instantiation of the same template-id (with a different, "
        "ODR-violating member order) in another module: whichever "
        "ClassTemplateSpecializationDecl is imported into the scratch AST "
        "context first wins, and referring to the other module's global of "
        "that same specialization afterwards fails with 'undeclared "
        "identifier'"
    )
    def test_stat_explicit_instantiation_vs_implicit_instantiation_combined_expr(self):
        """
        Tests LLDB's behaviour when the same template-id 'Stat<double>' is:
          - an explicit instantiation definition
            ('template struct Stat<double>;') of the primary template
            'template<typename T> struct Stat { T sum; T count; };' in the
            main executable (a ClassTemplateSpecializationDecl with
            TSK_ExplicitInstantiationDefinition), and
          - an ordinary implicit instantiation of a *differently shaped*
            primary template
            'template<typename T> struct Stat { T count; T sum; };' (fields
            swapped) in a dylib (a ClassTemplateSpecializationDecl with
            TSK_ImplicitInstantiation).

        This is a real ODR violation: the same specialization has two
        incompatible layouts across translation units, compounded by the
        two modules disagreeing about how the specialization came to exist
        (explicit-instantiation-definition bookkeeping vs. a plain implicit
        instantiation). Dumping each module's AST for 'Stat' and then
        evaluating an expression that references both modules' globals of
        that same specialization forces LLDB's expression evaluator to
        import and reconcile both conflicting AST nodes for 'Stat<double>'
        within the same target scratch AST context. The hope is that
        DWARFASTParserClang's/TypeSystemClang's logic for deciding whether
        an "already complete" specialization can be reused as-is, versus
        needing to be completed or re-instantiated, trips over the
        mismatched specialization-kind state badly enough to crash instead
        of merely producing a wrong-but-well-formed value.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Dump both modules' ASTs for 'Stat' before combining them in a
        # single expression, to force LLDB to have parsed both conflicting
        # ClassTemplateSpecializationDecls for 'Stat<double>' from DWARF.
        self.expect("target modules dump ast --filter Stat")

        self.expect_expr(
            "main_stat.sum + plugin_stat.sum",
            result_type="double",
            result_value="5",
        )
