import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpAstRecursiveSelfFriendOdrTestCase(TestBase):
    def test_dump_ast_recursive_self_referential_node_across_conflicting_modules(
        self,
    ):
        """
        Tests LLDB's "target modules dump ast" command against a self-
        referential linked-list type that is also a genuine ODR violation
        across two modules:

          - The main executable's 'Node':
                struct Node { Node *next; friend struct Node; int data; };
            ('friend struct Node;' is a legal, no-op self-friend
            declaration -- it doesn't change layout or semantics, but
            gives DWARFASTParserClang something slightly unusual to
            chew on while completing a self-referential RecordDecl.)

          - The dylib's 'Node' (see plugin.cpp):
                struct Node { int data; Node *next; };
            Same name, same field names/types, but reordered fields and
            no friend declaration.

        Both are self-referential via 'next', so DWARF encodes the
        pointee type recursively in both modules' debug info. The main
        executable instantiates a 3-node linked list in its global scope
        ('g_node1' -> 'g_node2' -> 'g_node3'); the dylib instantiates a
        separate 2-node linked list ('g_dylib_node1' -> 'g_dylib_node2')
        using the reordered layout.

        Running "target modules dump ast --filter Node" against each
        module individually, back-to-back, forces two structurally
        different 'Node' RecordDecls (same name, different field order)
        to each get fully completed -- including their self-referential
        'Node *' fields -- in the same LLDB session. Running the same
        command a third time with no module argument forces the dumper
        to scan every loaded image and print the self-referential
        'Node *' field for both conflicting layouts in a single pass.

        If the AST dumper's cycle-detection for self-referential pointer
        types relied on cross-module assumptions (e.g. assuming a given
        type name maps to a single Decl identity, or reusing dumper
        state across the two dumps of structurally different 'Node's),
        this could corrupt the dumper's internal state and crash LLDB
        (e.g. via unbounded recursion) instead of merely printing
        something unexpected. At a minimum, every command below must
        complete without crashing LLDB, regardless of whether the
        printed layout is faithful to any single module.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Force the main executable's own DWARFASTParserClang to fully
        # complete its self-referential 'Node' (including chasing 'next'
        # through the 3-node list), independently of the dylib's 'Node'.
        self.expect_expr("g_node1.data", result_type="int", result_value="1")
        self.expect_expr(
            "g_node1.next->next->data", result_type="int", result_value="3"
        )

        # Force the dylib's own DWARFASTParserClang to fully complete its
        # (reordered, ODR-conflicting) self-referential 'Node', via the
        # dylib's separate 2-node list.
        self.expect_expr("g_dylib_node1.data", result_type="int", result_value="10")
        self.expect_expr(
            "g_dylib_node1.next->data", result_type="int", result_value="20"
        )

        # Dump the main executable's 'Node' completion on its own. This
        # has to print the self-referential 'next' field without
        # recursing forever.
        self.expect("target modules dump ast --filter Node a.out")

        # Immediately dump the dylib's differently-shaped 'Node'
        # completion, in the same session, right after the main
        # executable's dump above.
        self.expect("target modules dump ast --filter Node libplugin.dylib")

        # With no module argument, this scans every loaded image and
        # unions together every filter-matched result -- printing both
        # conflicting, self-referential 'Node' completions (main
        # executable's and the dylib's) back to back in a single pass.
        # This must not crash, regardless of whether the two printed
        # layouts agree with each other.
        self.expect("target modules dump ast --filter Node")

        # Separately, dump the shared per-target scratch
        # TypeSystem/ASTContext to inspect whatever state the expression
        # evaluations above left behind.
        self.expect("target dump typesystem")

        # Exercise the merged/half-merged state a bit more and dump
        # again, in case corruption only shows up after further use.
        self.expect_expr(
            "g_node1.next->next->data + g_dylib_node1.next->data",
            result_type="int",
            result_value="23",
        )
        self.expect("target modules dump ast --filter Node")
        self.expect("target dump typesystem")
