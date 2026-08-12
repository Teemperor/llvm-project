"""
Test LLDB's behaviour with two class template specializations of the same
name and template argument ('Choice<int>') across a main executable and a
dylib, where the data layout is identical (a single 'int data' member) but
the *method set* diverges: a member function template 'pick()' is gated by
'std::enable_if<sizeof(U) == 4, int>::type' in the main executable (so it
exists and is instantiated for 'Choice<int>') and by the inverted condition
'std::enable_if<sizeof(U) != 4, int>::type' in the dylib (so it does not
exist at all for 'Choice<int>' -- it is SFINAE'd away).

This differs from the already-covered 'odr-template-body-mismatch' scenario
(same specialization, different *data* layout): here the two RecordDecls
agree on layout and only disagree on which SFINAE branch of a dependent,
nested ('typename enable_if<...>::type') return type "won" for the member
function template.

A real crash was found while exploring this scenario, but it is NOT
exercised by this test file (see 'test_both_together' below for why):
after evaluating an expression that references 'Choice<int>' (e.g.
'main_choice.data'), running 'target modules dump ast --filter Choice'
segfaults LLDB outright via unbounded recursion in the AST-dumping
RecursiveASTVisitor when it walks the SFINAE'd, 'std::enable_if'-returning
member function template's dependent return type. This reproduces even for
a single, non-ODR-conflicting 'Choice<int>' in one module alone (i.e. it is
not specific to the cross-module conflict, only to the presence of the
SFINAE'd member function template); deliberately not exercised here since a
real segfault would take down the whole test process instead of failing
cleanly. This is the same underlying 'target modules dump ast' recursion
bug family as the one documented in 'weird-ast-crtp-self-base-odr', but
triggered via a SFINAE member function template's dependent return type
rather than a CRTP self-referential base class.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstSfinaeOverloadSetDivergentOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each module's 'Choice<int>' can be read on its own, before the
        other module's conflicting definition has been imported into the
        scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_choice.data", result_type="int", result_value="42")
        self.expect_expr(
            "gPluginChoice->data", result_type="int", result_value="99"
        )

    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same template specialization
        ('Choice<int>') has two definitions that agree on layout ('int
        data' in both) but disagree on their SFINAE-gated method set:
        main.cpp's 'Choice<int>' has a callable 'pick()' (the 'sizeof(U)
        == 4' branch of 'std::enable_if' is selected), while plugin.cpp's
        'Choice<int>' has no 'pick()' at all (the inverted condition
        SFINAEs it away for 'int').

        Evaluating an expression that combines both modules' 'data' fields
        forces the ASTImporter to import and reconcile both conflicting
        'Choice<int>' RecordDecls (and their differing method lists) into
        the same scratch AST context. This works fine and returns the
        correct, module-specific values without crashing.

        Note: 'main_choice.pick()' itself does not evaluate successfully
        even in isolation (see 'test_pick_is_not_resolved_by_expr' below)
        because LLDB's expression evaluator does not surface a member
        function *template* instantiation coming purely from DWARF as a
        callable member -- that limitation is orthogonal to the ODR
        conflict explored here.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the main executable's 'Choice<int>' first, so its
        # (SFINAE'd-in 'pick()') definition ends up in the scratch AST
        # context.
        self.expect_expr("main_choice.data", result_type="int", result_value="42")

        # Now bring in the dylib's conflicting 'Choice<int>' (no 'pick()'
        # at all) into the same expression, forcing the ASTImporter to
        # reconcile both method sets at once.
        self.expect_expr(
            "gPluginChoice->data", result_type="int", result_value="99"
        )

        # Combine both modules' conflicting 'Choice<int>' in a single
        # expression.
        self.expect_expr(
            "main_choice.data + gPluginChoice->data",
            result_type="int",
            result_value="141",
        )

    @expectedFailureAll(
        bugnumber="LLDB's expression evaluator does not resolve a member "
        "function template (here, one gated by std::enable_if/SFINAE) as a "
        "callable member coming from DWARF debug info, even when it has "
        "been instantiated and is otherwise unambiguous -- unrelated to "
        "any ODR conflict, this reproduces for a single, non-conflicting "
        "'Choice<int>' in one module alone"
    )
    def test_pick_is_not_resolved_by_expr(self):
        """
        Documents that 'pick()' cannot be called via the expression
        evaluator at all, even from the main executable's own (internally
        consistent, SFINAE-resolved) 'Choice<int>'. This is a pre-existing
        DWARF/member-function-template limitation, not a consequence of
        the ODR conflict explored by the other tests in this file.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_choice.pick()", result_type="int", result_value="1")
