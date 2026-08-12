"""
Test LLDB's handling of a three-way ODR conflict on the same struct tag
name spread across three different dylibs (a "naming cycle").
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTripleModuleCycleOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's expression evaluator against three dylibs that each
        define an incompatible 'struct Cycle':
          - dylib1: struct Cycle { int tag; };
          - dylib2: struct Cycle { int tag; int extra1; };
          - dylib3: struct Cycle { long tag; double extra2; };

        The main executable links all three dylibs and only forward
        declares 'Cycle' itself, holding one global pointer per dylib
        (gCycle1, gCycle2, gCycle3). Evaluating a single expression that
        reads a field out of all three globals forces LLDB's
        DWARFASTParserClang/ASTImporter machinery to import and reconcile
        three simultaneously-conflicting definitions of the same tag name
        into the shared scratch AST context.

        Two-way ODR conflicts (see odr-handling-with-dylib and friends)
        are already known to be dicey; this test goes one step further
        with a three-way conflict to check whether any "at most two
        conflicting versions" assumption in the conflict-detection/import
        bookkeeping gets violated once a third incompatible decl for the
        same name shows up.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "cycle_entry", lldb.SBFileSpec("main.cpp")
        )

        # Read a field out of each of the three (mutually incompatible)
        # 'Cycle' globals in a single expression. This is the point where
        # LLDB has to import/reconcile all three conflicting definitions
        # of 'struct Cycle' at once.
        self.expect_expr(
            "gCycle1->tag + gCycle2->extra1 + (long)gCycle3->tag",
            result_type="long",
            result_value="24",
        )
