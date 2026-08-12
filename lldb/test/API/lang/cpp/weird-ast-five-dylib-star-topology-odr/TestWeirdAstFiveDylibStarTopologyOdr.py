"""
Test LLDB's handling of a five-way ODR conflict on the same struct tag
name ('Payload') spread across five leaf dylibs that all feed into one
hub dylib (a "star topology").
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFiveDylibStarTopologyOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's expression evaluator against five leaf dylibs that
        each define an incompatible 'struct Payload':
          - Leaf1: struct Payload { int kind; int data; };
          - Leaf2: struct Payload { int kind; double data; };
          - Leaf3: struct Payload { int kind; char *data; };
          - Leaf4: struct Payload { int kind; bool data; };
          - Leaf5: struct Payload { int kind; struct { short s; } data; };

        The hub dylib ('Hub') links all five leaves and aggregates one
        opaque 'Payload *' from each into a single 'AllPayloads' struct,
        without ever seeing any of the five conflicting definitions
        itself (it only forward declares the 'Payload' tag). The main
        executable links Hub plus all five leaves and only forward
        declares 'AllPayloads', obtaining a pointer to it from
        MakeAllPayloads().

        Evaluating a single expression that reads the 'data' field out
        of all five 'hub->p[N]' pointers back-to-back forces LLDB's
        DWARFASTParserClang/ASTImporter machinery to import and reconcile
        five simultaneously-conflicting definitions of the same tag name
        ('Payload') into the shared per-target scratch AST context, one
        right after another within a single expression evaluation. This
        maximizes the chance of exercising any bookkeeping in the
        ASTImporter (e.g. its "already imported"/origin-tracking maps)
        that assumes at most two conflicting versions of a decl can ever
        be in flight at once.

        This is a strict superset of the three-way conflict already
        covered by weird-ast-triple-module-cycle-odr: here there are five
        distinct incompatible shapes (including a pointer type and an
        anonymous nested struct, not just different scalar members), and
        they are spread across a hub-and-spoke module graph rather than a
        flat list of independent dylibs.

        Whichever leaf's 'struct Payload' the ASTImporter happens to
        import into the scratch AST context first "wins": every
        'hub->p[N]->data' access afterwards gets reinterpreted using that
        winning definition's layout, regardless of which leaf a given
        pointer actually points into. This is expected to produce
        wrong-but-well-formed values (not a crash) since the type
        resolution result is not fully deterministic across all N.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "hub_entry", lldb.SBFileSpec("main.cpp")
        )

        # This should not crash LLDB. Whichever of the five conflicting
        # 'struct Payload' definitions wins the race to be the
        # authoritative one in the scratch AST context, all five
        # 'hub->p[N]->data' expressions below will be evaluated using
        # that single (possibly wrong, for most of the five pointers)
        # layout -- but evaluation must complete without an internal
        # error, assertion, or crash.
        self.expect(
            "expr hub->p[0]->data, hub->p[1]->data, hub->p[2]->data, "
            "hub->p[3]->data, hub->p[4]->data"
        )
