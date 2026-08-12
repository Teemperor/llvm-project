"""
Test LLDB's handling of a diamond-shaped ODR conflict on the tag name
'Node', where the SAME translation unit (plugin.cpp, playing the role of
"Top") also contributes two structurally different 'Node' declarations of
its own on top of the two cross-dylib variants.

The diamond:
  - Base (its own dylib): struct Base { int id; };
  - Left (its own dylib, #includes base.h): privately redefines, in an
    anonymous namespace, 'struct Node { Base *parent; int tag; };'
  - Right (its own dylib, #includes base.h): privately redefines, in its
    own anonymous namespace, 'struct Node { Base *parent; long tag; };'
    (same spelling as Left's Node, but 'tag' is 'long' instead of 'int' --
    a genuine ODR violation once both are visible together.)
  - Top (the 'plugin' dylib here): links against Left and Right and holds
    a Node* obtained from each. It *also* defines two of its own
    "Node"-spelled types via two different header include paths: node.h
    (a function-local 'struct Node' with a 'short tag') and node_alias.h
    (a *different* function-local 'struct Node', also with a 'short tag'
    but with an extra trailing field, reached via a different include
    guard so it is genuinely parsed as a second, distinct declaration in
    the same compile unit as node.h's).

This means Top's own translation unit contributes two structurally
different DWARF RecordDecls both spelled "Node", *in addition to* the two
cross-dylib "Node" variants from Left and Right. Evaluating an expression
that walks all four "Node" pointers (and follows 'parent' back to the
shared 'Base' at the apex of the diamond) forces LLDB's
DWARFASTParserClang/ASTImporter machinery to reconcile four simultaneously
ambiguous, mutually incompatible definitions of the same tag name --
including a self-import merge attempt within a single module's own
DWARF-derived AST, which is a much less exercised code path than the
regular cross-module case.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDiamondCycleSelfConflictOdrTestCase(TestBase):
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Walk Left's Node -> parent (Base), Right's Node -> parent (Base),
        # and Top's own two locally-defined "Node" variants, all in one
        # expression. Left's and Right's Node both point at the same
        # underlying Base instance (id == 42), so a correct evaluation
        # that resolves '->parent->id' for each adds up to 84; Top's own
        # two Node variants have a zero-initialized 'tag' that was never
        # written to, so they contribute 0 each. This should not crash
        # LLDB, regardless of which of the four conflicting "Node"
        # declarations name lookup happens to settle on for the cast.
        self.expect_expr(
            "((Node*)gTop.leftNode)->parent->id + ((Node*)gTop.rightNode)->parent->id "
            "+ (long)((Node*)gTop.localNodeV1)->tag + (long)((Node*)gTop.localNodeV2)->tag",
            result_type="long",
            result_value="84",
        )

        # Dumping the merged scratch AST and the per-module ASTs afterwards
        # should also not crash, even though the scratch context now holds
        # multiple mutually-incompatible "Node" definitions (including two
        # that both originated from Top's own translation unit).
        self.expect("target dump typesystem", substrs=["State of scratch Clang type system"])
        self.expect("target modules dump ast --filter Node")
