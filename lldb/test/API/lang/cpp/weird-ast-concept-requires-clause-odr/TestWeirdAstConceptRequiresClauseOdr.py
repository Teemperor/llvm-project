import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstConceptRequiresClauseOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when the exact same class template
        specialization ('Adder<int>') is constrained by two C++20 concepts
        that share a name ('Addable') but have differently-worded
        requires-clauses: one requires only operator+, the other requires
        both operator+ and operator-. One definition lives in the main
        executable, the other in a dylib.

        Unlike odr-template-body-mismatch (which varies the template body
        itself), this varies only the *constraint* attached to the
        template parameter. DWARF has no representation for concepts or
        requires-clauses at all, so LLDB's DWARF-based reconstruction of
        both 'Adder<int>' specializations is byte-for-byte identical
        (neither carries any trace of the concept). This test evaluates an
        expression that combines both specializations, and dumps the
        per-module Clang ASTs to document that state, in case a future
        change to DWARFASTParserClang/ASTImporter starts attaching
        constraint information and mishandles the resulting ODR conflict.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Dump both modules' ASTs for 'Adder' before combining them in a
        # single expression, to force LLDB to have parsed both conflicting
        # ClassTemplateDecls/ClassTemplateSpecializationDecls for
        # 'Adder<int>' from DWARF. Neither dump should contain any trace of
        # a constraint/requires-clause, since DWARF doesn't carry that
        # information at all; this documents the "same-looking" state that
        # the ASTImporter has to reconcile despite the two 'Addable'
        # concepts genuinely disagreeing about what 'T' must support.
        self.expect(
            "target modules dump ast --filter Adder",
            substrs=["ClassTemplateDecl", "Adder", "ClassTemplateSpecialization"],
        )

        # Using both conflicting 'Adder<int>' specializations (each
        # constrained by a differently-worded 'Addable' concept) in the
        # same expression shouldn't crash, and should evaluate correctly.
        self.expect_expr(
            "main_adder.lhs + gPluginAdder->rhs",
            result_type="int",
            result_value="21",
        )
