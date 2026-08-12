"""
Test LLDB's handling of two dylibs that each claim to be the sole
'inline namespace' child of the same enclosing namespace 'lib', but
disagree on the inline namespace's version name (v1 vs v2) and on the
shape of the 'Widget' type living inside it. This is invalid C++ (only
one inline namespace child can be the canonical way to reach 'lib::X')
but perfectly achievable at the DWARF/debug-info level, since the two
dylibs are compiled completely independently of each other.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstInlineNamespaceVersionSkewOdrTestCase(TestBase):
    def test_access_through_pointer(self):
        """
        Dereferencing/accessing members through the dylib-owned globals
        (whose static type is derived straight from each dylib's own
        DWARF, not from a name lookup of 'lib::Widget' typed by the user)
        works fine and produces the correct, distinct values for each
        dylib's incompatible 'lib::Widget'.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "ns_entry", lldb.SBFileSpec("main.cpp")
        )

        # DylibA's lib::v1::Widget only has 'a'.
        self.expect_expr("gWidgetA->a", result_type="int", result_value="1")
        # DylibB's lib::v2::Widget has 'a' and 'b'.
        self.expect_expr("gWidgetB->a", result_type="int", result_value="2")
        self.expect_expr("gWidgetB->b", result_type="double", result_value="2.5")

        # Referencing both dylibs' conflicting 'lib::Widget' globals in
        # the same expression forces the ASTImporter to reconcile 'lib'
        # having two different inline namespace children ('v1' and 'v2')
        # at once. This should not crash.
        self.expect_expr(
            "gWidgetA->a + gWidgetB->a", result_type="int", result_value="3"
        )

        # Fully materializing both conflicting definitions (as opposed to
        # only accessing a single field through a pointer) pulls the full
        # 'struct Widget' definitions -- and the two disagreeing inline
        # namespaces 'v1'/'v2' living under 'lib' -- into the scratch AST
        # context.
        self.expect_expr("*gWidgetA", result_type="lib::Widget")
        self.expect_expr("*gWidgetB", result_type="lib::Widget")

        # The merged scratch AST context should now contain both 'v1' and
        # 'v2' as (incompatible) inline children of 'lib', without lldb
        # crashing while walking/printing it.
        self.expect("target dump typesystem", substrs=["lib", "Widget"])

    @expectedFailureAll(
        bugnumber="ASTImporter/TypeSystemClang can't unambiguously resolve "
        "an explicitly-typed qualified name ('lib::Widget') once two "
        "dylibs have each contributed a *different* inline namespace "
        "(differing version names 'v1' vs 'v2') as the sole inline child "
        "of the same enclosing namespace 'lib': the qualified lookup used "
        "when parsing a type name in the expression parser fails with "
        "'no type named Widget in namespace lib', even though unqualified "
        "member access through an already-typed pointer (e.g. "
        "'gWidgetA->a') resolves the very same type without any problem."
    )
    def test_explicit_qualified_typename(self):
        """
        Once both dylibs' conflicting inline namespaces have been merged
        into the scratch AST context, naming the type 'lib::Widget'
        explicitly (as opposed to letting it be inferred from an
        already-typed pointer/value) should still work, since at least
        one 'lib::Widget' is genuinely reachable via qualified lookup
        through 'lib's inline namespace. Currently it does not.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "ns_entry", lldb.SBFileSpec("main.cpp")
        )

        # Make sure both conflicting definitions have been imported into
        # the shared scratch AST context first.
        self.expect_expr("gWidgetA->a", result_type="int", result_value="1")
        self.expect_expr("gWidgetB->a", result_type="int", result_value="2")

        # Now try to name the (ambiguous) qualified type directly.
        self.expect_expr("sizeof(lib::Widget)", result_type="unsigned long")
