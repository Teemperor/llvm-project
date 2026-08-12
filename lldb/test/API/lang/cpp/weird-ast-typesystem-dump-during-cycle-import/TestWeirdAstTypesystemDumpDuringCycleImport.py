"""
Test LLDB's handling of a mutual cross-dylib reference cycle combined with
a two-way ODR conflict on both endpoint types.

Dylib A defines the real 'struct Wrapper { B_Type *other; int a; };' and
only forward-declares 'B_Type' (whose real definition lives in dylib B).
Dylib B defines the real 'struct B_Type { Wrapper *other; int b; };' and
only forward-declares 'Wrapper' (whose real definition lives in dylib A).

On top of that mutual reference cycle, each dylib ALSO privately defines
its own conflicting full definition of the *other* dylib's type, used only
for local pointer arithmetic:
  - dylib A privately defines 'struct B_Type { char mismatch[99]; };'
  - dylib B privately defines 'struct Wrapper { char mismatch[77]; };'

Evaluating an expression that walks the cycle two hops deep (e.g.
'a_get_wrapper()->other->other->a') forces LLDB's
DWARFASTParserClang/ASTImporter machinery to import and reconcile both
mutually-referencing types *and* both of their ODR-conflicting private
stub definitions into the shared per-target scratch AST context at once.

This test also exercises 'target dump typesystem' (which dumps the
target's shared scratch Clang AST/TypeSystem) at multiple points around
that cross-import, including while an expression evaluation is genuinely
still "in flight" (stopped inside the JIT-compiled expression, before the
importer's bookkeeping for that expression has been unwound), to check
that dumping never crashes LLDB even when the scratch AST holds multiple
incompatible definitions of the same tag names.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTypesystemDumpDuringCycleImportTestCase(TestBase):
    def test_each_side_alone(self):
        """
        Each dylib's real type can be evaluated fine on its own, and
        'target dump typesystem' does not crash while doing so.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "cycle_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("a_get_wrapper()->a", result_type="int", result_value="42")
        self.expect_expr("b_get_type()->b", result_type="int", result_value="99")

        # Force full type import (a plain '->a'/'->b' field access on an
        # int doesn't need the whole struct to be imported into the
        # scratch AST) so 'target dump typesystem' below actually has
        # something interesting to look at.
        self.expect_expr("*a_get_wrapper()", result_type="Wrapper")
        self.expect_expr("*b_get_type()", result_type="B_Type")

        # Dumping the scratch typesystem after pulling in both (so far
        # non-conflicting) real definitions should never crash LLDB.
        self.expect("target dump typesystem", substrs=["Wrapper", "B_Type"])

    @expectedFailureAll(
        bugnumber="ASTImporter resolves the ODR-conflicting 'B_Type'/"
        "'Wrapper' tag names to whichever definition was imported first "
        "(here, each dylib's own PRIVATE conflicting stub of the other "
        "dylib's type), so walking the real mutual reference cycle two "
        "hops deep ('a_get_wrapper()->other->other->a') fails with "
        "'no member named other' instead of returning the real value"
    )
    def test_cross_cycle_two_hops(self):
        """
        Walking the real mutual A<->B reference cycle two hops deep should
        ideally evaluate to the original field value, but currently fails
        because the scratch AST's 'B_Type'/'Wrapper' lookups resolve to
        the wrong (ODR-conflicting private stub) definition.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "cycle_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr(
            "a_get_wrapper()->other->other->a", result_type="int", result_value="42"
        )

    def test_dump_typesystem_survives_cross_import_and_odr_conflict(self):
        """
        Regardless of whether the cross-cycle expression above succeeds or
        fails, 'target dump typesystem' must never crash LLDB itself, even
        right after an ODR-conflicted cross-dylib import attempt, and even
        while an expression evaluation is genuinely still "in flight"
        (stopped inside the JIT-compiled expression before that
        expression's importer bookkeeping has been unwound).
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "cycle_entry", lldb.SBFileSpec("main.cpp")
        )

        # Force import of both real types plus both ODR-conflicting
        # private stubs into the scratch AST context.
        self.runCmd("expr *a_get_wrapper()")
        self.runCmd("expr *b_get_type()")
        self.runCmd("expr *a_get_wrapper()->other")
        self.runCmd("expr *b_get_type()->other")

        # This is the operation under test: dumping the scratch
        # TypeSystem/ASTContext while it holds two incompatible
        # definitions each of 'B_Type' and 'Wrapper' at once. This should
        # complete without crashing LLDB, regardless of the exact textual
        # output.
        self.expect("target dump typesystem", substrs=["Wrapper", "B_Type"])

        # Try (and expect to fail gracefully rather than crash) the
        # cross-cycle expression, then dump again immediately afterwards.
        self.runCmd("expr a_get_wrapper()->other->other->a", check=False)
        self.expect("target dump typesystem", substrs=["Wrapper", "B_Type"])

        # Now set an internal breakpoint on a function called from *inside*
        # the JIT-compiled expression wrapper, so that evaluating the
        # expression traps mid-evaluation: the process is left stopped
        # inside the callee, with the expression's own import/allocation
        # bookkeeping not yet unwound ("in flight"). Dump the scratch
        # typesystem from exactly that state.
        target = self.target()
        inner_bp = target.BreakpointCreateByName("a_get_wrapper")
        self.assertTrue(inner_bp.IsValid())
        self.assertEqual(inner_bp.GetNumLocations(), 1)

        frame = self.thread().GetSelectedFrame()
        options = lldb.SBExpressionOptions()
        options.SetIgnoreBreakpoints(False)
        options.SetUnwindOnError(False)

        result = frame.EvaluateExpression("*a_get_wrapper()->other", options)
        self.assertTrue(result.GetError().Fail())

        # We should now be stopped inside a_get_wrapper(), called from the
        # still-unfinished JIT-compiled expression above it on the stack.
        stopped_frame = self.process().GetSelectedThread().GetSelectedFrame()
        self.assertIn("a_get_wrapper", stopped_frame.GetFunctionName())

        self.expect("target dump typesystem", substrs=["Wrapper", "B_Type"])
        self.expect("bt", substrs=["a_get_wrapper"])

        # Unwind back out of the trapped expression before finishing.
        self.runCmd("thread return -x", check=False)
        self.expect("target dump typesystem", substrs=["Wrapper", "B_Type"])
