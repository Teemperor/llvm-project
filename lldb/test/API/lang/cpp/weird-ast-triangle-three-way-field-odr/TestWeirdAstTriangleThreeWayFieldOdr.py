"""
Test LLDB's handling of a three-way field-type ODR conflict on the same
struct tag name ('Triangle') spread across three different dylibs, where
main.cpp only ever sees a forward declaration and holds one pointer per
dylib (obtained from each dylib's factory function).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTriangleThreeWayFieldOdrTestCase(TestBase):
    def test_consistent_field(self):
        """
        The first field ('x') has the exact same type ('int') at the exact
        same offset in all three dylibs, so reading it through all three
        pointers in a single expression should always produce the correct,
        unambiguous result regardless of which dylib's 'Triangle' ends up
        winning the ASTImporter's tag-name merge.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "triangle_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("gA->x + gB->x + gC->x", result_type="int", result_value="6")

    @expectedFailureAll(
        bugnumber="ASTImporter merges three mutually-incompatible dylib "
        "definitions of 'struct Triangle' (different 'val' field type in "
        "each: int/float/char[8]) into a single RecordDecl in the shared "
        "per-target scratch AST context. Whichever dylib's definition is "
        "imported first (this is load-order dependent and not stable "
        "across runs) wins, and the *other* two pointers get their 'val' "
        "field silently reinterpreted using the winning definition's type "
        "instead of their own dylib's actual type, producing well-formed "
        "but wrong values instead of the correct per-dylib types."
    )
    def test_conflicting_field(self):
        """
        Dereference all three pointers in sequence in a single expression,
        forcing the ASTImporter to import/merge 'Triangle' three times into
        the scratch context with three mutually-incompatible field types
        for 'val':
          - DylibA: struct Triangle { int x; int val; };
          - DylibB: struct Triangle { int x; float val; };
          - DylibC: struct Triangle { int x; char val[8]; };

        Correct behavior would be for each dereference to use its own
        dylib's actual field type for 'val' (int, then float, then
        char[8]). Instead, all three end up reporting the *same* (single
        merged) type, i.e. at least two of the three are wrong.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "triangle_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("gA->val", result_type="int", result_value="100")
        self.expect_expr("gB->val", result_type="float", result_value="2.5")
        self.expect_expr("gC->val", result_type="char[8]", result_value='"hello"')
