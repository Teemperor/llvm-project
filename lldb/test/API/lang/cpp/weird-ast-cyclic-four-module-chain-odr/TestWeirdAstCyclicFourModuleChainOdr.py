"""
Test LLDB's handling of a four-way ODR conflict on the same struct tag name
('Ring'), spread across four dylibs that are linked into a genuine circular
*runtime* linked list (not just a naming cycle): d1's Ring -> d2's Ring ->
d3's Ring -> d4's Ring -> back to d1's Ring.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstCyclicFourModuleChainOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's expression evaluator against four dylibs that each
        define an incompatible 'struct Ring':
          - d1: struct Ring { Ring *next; int payload; };
          - d2: struct Ring { Ring *next; double payload; };
          - d3: struct Ring { Ring *next; struct { int a, b; } payload; };
          - d4: struct Ring { Ring *next; void *payload; };

        The main executable links all four dylibs and only forward declares
        'Ring' itself, holding one global pointer per dylib (g1, g2, g3,
        g4). At runtime those four globals are linked into a genuine
        circular linked list: g1->next == g2, g2->next == g3, g3->next ==
        g4, and g4->next == g1. Walking the chain from g1 (following
        '->next' four times) therefore lands back on the very same object
        that g1 points to, but each hop passes through a differently-shaped
        (ODR-violating) definition of 'struct Ring'.

        Unlike a simple DAG of imports, this forces LLDB's
        DWARFASTParserClang/ASTImporter machinery to reconcile the same tag
        name 'Ring' reappearing after a full cycle of conflicting merges,
        which is exactly the kind of import history a cycle-detection
        algorithm that assumes DAG-like import order could mishandle.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "ring_entry", lldb.SBFileSpec("main.cpp")
        )

        # Walking four '->next' hops from g1 gets back to the same object
        # that g1 itself points to (the runtime cycle is fully intact),
        # even though each hop's static type is an incompatible 'struct
        # Ring' pulled in from a different dylib.
        self.expect_expr(
            "g1->next->next->next->next == g1", result_type="bool", result_value="true"
        )

        # Dumping the shared per-target scratch AST context after walking
        # the full cycle shouldn't crash LLDB, regardless of how many
        # mutually-incompatible 'Ring' definitions got merged into it along
        # the way.
        self.expect("target dump typesystem", substrs=["Ring"])

        # Evaluating the field access at the end of the chain shouldn't
        # crash either. Because the ASTImporter reconciles same-named tag
        # types by keeping whichever definition of 'Ring' was imported into
        # the scratch context first, the *value* read back here is not
        # guaranteed to be the mathematically "correct" d1 payload -- only
        # that reading it is well-formed and doesn't crash.
        self.expect("expression g1->next->next->next->next->payload")
