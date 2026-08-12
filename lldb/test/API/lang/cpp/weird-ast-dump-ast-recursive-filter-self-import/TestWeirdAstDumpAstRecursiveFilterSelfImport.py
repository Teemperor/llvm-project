"""
Test LLDB's robustness when "target dump typesystem" is repeatedly
invoked, within the same stop, against a shared scratch AST context that
holds two conflicting, self-referential shapes of the same qualified
name ('app::Ring') imported from two different dylibs.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpAstRecursiveFilterSelfImportTestCase(TestBase):
    def test_dump_typesystem_repeated_after_conflicting_self_referential_imports(
        self,
    ):
        """
        Tests LLDB's "target dump typesystem" command against a shared
        scratch AST context that has been made to hold two conflicting,
        self-referential shapes of the same qualified name at once:

          - Dylib A's 'app::Ring' (see DylibA.h):
                namespace app { struct Ring { app::Ring *next; int val; }; }

          - Dylib B's 'app::Ring' (see DylibB.h):
                namespace app {
                  struct Ring { app::Ring *next; int val; int tag; };
                }

        Both shapes are self-referential via a pointer-to-self field
        ('next'): resolving that field's pointee type requires the
        RecordDecl for 'app::Ring' itself, in the very module that is
        completing it. The two shapes share the same qualified name and
        the same leading two fields, differing only by dylib B's extra
        trailing 'tag' field -- a genuine ODR violation across the two
        dylibs.

        Each dylib also builds its own private 3-node *circular* linked
        list (dylibA_make_ring()/dylibB_make_ring(), each re-linking
        three static globals into a cycle and returning the head), so
        that evaluating a chain of '->next' accesses on either list
        walks all the way around and back to the start.

        The test imports both conflicting shapes into the shared,
        per-target scratch TypeSystemClang/ASTContext via two separate
        'expr' calls -- one per dylib's allocator -- so that the
        ASTImporter has to reconcile two different concrete completions
        of the self-referential 'app::Ring' RecordDecl under the same
        qualified name, simultaneously, in the same scratch context.

        It then repeatedly runs "target dump typesystem" (which walks
        every Decl currently sitting in the scratch ASTContext) five
        times in a row within the same stop, each preceded by a trivial
        "expr (void)0" specifically to give any lazy re-validation/
        re-completion logic in TypeSystemClang or the ASTImporter a
        chance to run again on an already-(partially-)merged context.

        If the ASTImporter's structural-equivalence/cycle-detection
        logic for a self-referential record keyed its "already
        importing this Decl" guard on the wrong identity (e.g. a
        specific redeclaration instead of the canonical Decl, or
        vice-versa) once a second, differently-shaped definition of the
        same qualified name entered the picture, resolving "app::Ring's
        'next' field's pointee" could require the merge of 'app::Ring'
        to already be complete, recursing without ever tripping the
        importer's own cycle guard -- i.e. unbounded recursion and a
        stack-overflow crash, plausibly during one of the *later*
        "target dump typesystem" calls (forced re-validation) rather
        than during the original 'expr' that performed the import.

        This test does not assert anything about the specific textual
        content of any individual dump (under a genuine ODR conflict on
        a self-referential type, which shape ends up "winning" in the
        shared scratch context can depend on import order and is not
        something this test should pin down). The only thing asserted
        is that every command below -- including all five repeated
        dumps -- completes without crashing LLDB.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "ring_entry", lldb.SBFileSpec("main.cpp")
        )

        # Import dylib A's self-referential 'app::Ring' into the shared
        # scratch AST context by allocating and walking all the way
        # around its private 3-node circular linked list.
        self.expect_expr(
            "((app::Ring *)dylibA_make_ring())->next->next->next->val",
            result_type="int",
            result_value="1",
        )

        # Now import dylib B's differently-shaped (extra 'tag' field)
        # 'app::Ring' under the very same qualified name, into the same
        # shared scratch AST context, again walking all the way around
        # its own private 3-node circular linked list.
        self.expect_expr(
            "((app::Ring *)dylibB_make_ring())->next->next->next->val",
            result_type="int",
            result_value="10",
        )

        # The scratch context now holds two conflicting, self-
        # referential completions of 'app::Ring' at once. Repeatedly
        # dump the shared scratch TypeSystem/ASTContext, five times in
        # a row within this same stop, each preceded by a trivial
        # expression evaluation to force any lazy re-validation or
        # re-completion logic to run again against the (partially-)
        # merged state left behind above. None of this must crash
        # LLDB, regardless of which shape's fields the dump ends up
        # showing for 'app::Ring'.
        for _ in range(5):
            self.expect_expr("(void)0")
            self.expect("target dump typesystem")

        # Exercise the merged/half-merged state a bit more from both
        # dylibs' sides again, and dump once more, in case corruption
        # only shows up after further back-and-forth use.
        self.expect_expr(
            "((app::Ring *)dylibA_make_ring())->next->val", result_type="int"
        )
        self.expect_expr(
            "((app::Ring *)dylibB_make_ring())->next->val", result_type="int"
        )
        self.expect("target dump typesystem")

        # Finally, cross-check against the per-module AST dumps (a
        # separate, DWARF-derived TypeSystemClang per module) for each
        # dylib individually, and then with no module argument at all
        # (which scans every loaded image and prints every filter-
        # matched result back to back in a single pass). None of these
        # must crash either.
        self.expect("target modules dump ast --filter Ring libDylibA.dylib")
        self.expect("target modules dump ast --filter Ring libDylibB.dylib")
        self.expect("target modules dump ast --filter Ring")
