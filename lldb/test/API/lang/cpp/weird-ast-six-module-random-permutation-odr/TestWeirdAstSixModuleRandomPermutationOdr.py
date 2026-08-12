"""
Test LLDB's handling of a six-way ODR violation on the array bound of the
same struct tag name ('Common') spread across six independent dylibs
(M1 .. M6), where main links all six and reads through all six conflicting
'Common *' pointers in a single expression before dumping every internal
Clang AST LLDB has ("target modules dump ast" with no filter, i.e. all six
dylibs plus main) immediately followed by a dump of the shared per-target
scratch TypeSystem/ASTContext ("target dump typesystem").
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstSixModuleRandomPermutationOdrTestCase(TestBase):
    def test_six_way_array_bound_odr_conflict_then_dump_all(self):
        """
        Tests LLDB's expression evaluator and internal-state dump commands
        against six dylibs that each define an incompatible 'struct Common':
          - M1: struct Common { int fields[1]; };
          - M2: struct Common { int fields[2]; };
          - M3: struct Common { int fields[3]; };
          - M4: struct Common { int fields[4]; };
          - M5: struct Common { int fields[5]; };
          - M6: struct Common { int fields[6]; };

        (See common.h.in for the shared header template and the Makefile
        for how 'sed' patches the array bound into six generated
        'commonN.h' copies at build time -- every copy still names the
        struct plain 'Common' via '#define COMMON_NAME Common'.)

        The main executable links all six dylibs, forward declares
        'struct Common' itself, and only ever obtains opaque 'Common *'
        pointers from each dylib's 'MakeCommon<N>()' factory -- so main
        never sees any of the six conflicting definitions of 'Common' at
        compile time. All six only get pulled into LLDB's
        ASTImporter/TypeSystemClang machinery -- and merged into the
        target's shared per-target scratch ASTContext -- when the
        expression below runs.

        Reading 'fields[0]' out of all six 'm<N>->fields[0]' pointers
        back-to-back in a single expression forces the ASTImporter to
        import and reconcile six simultaneously-conflicting completions of
        the same tag name ('Common') into the scratch AST context, one
        right after another within a single expression evaluation.
        Sequential array-bound conflicts merged into one scratch
        RecordDecl maximize the iteration count on whatever internal
        merge-retry or already-imported-decl bookkeeping the ASTImporter
        uses, which is the most likely place for an off-by-one or
        use-after-free in a rarely-exercised high-iteration-count branch
        of ASTImporter's field-merging logic to show up.

        Immediately afterwards, this dumps every parsed per-module Clang
        AST LLDB currently holds with an unfiltered 'target modules dump
        ast' (main plus all six dylibs), followed immediately by an
        unfiltered 'target dump typesystem' dump of the shared scratch
        ASTContext that the expression above just finished merging six
        conflicting 'Common' completions into. Running the two dump
        commands back-to-back like this, right after the six-way merge,
        stresses any global/static caches shared between them.

        Whichever of the six conflicting 'struct Common' definitions the
        ASTImporter happens to import into the scratch AST context first
        "wins": every 'm<N>->fields[0]' access is evaluated using that
        single winning layout, regardless of which dylib each pointer
        actually points into. This is expected to produce a
        wrong-but-well-formed sum (not a crash), and both dump commands
        are expected to complete without an internal error, assertion, or
        crash, even though they walk a merged 'Common' RecordDecl that is
        internally inconsistent with at least five of the six modules it
        claims to describe.
        """
        self.build()

        # Break after all six 'm<N> = MakeCommon<N>()' assignments have run,
        # so all six globals are populated by the time the test expression
        # below dereferences them.
        lldbutil.run_to_source_breakpoint(
            self, "Set breakpoint here", lldb.SBFileSpec("main.cpp")
        )

        # This should not crash LLDB. Whichever of the six conflicting
        # 'struct Common' definitions wins the race to be the
        # authoritative one in the scratch AST context, all six
        # 'm<N>->fields[0]' expressions below will be evaluated using
        # that single (possibly wrong, for most of the six pointers)
        # layout -- but evaluation must complete without an internal
        # error, assertion, or crash.
        self.expect(
            "expr m1->fields[0] + m2->fields[0] + m3->fields[0] + "
            "m4->fields[0] + m5->fields[0] + m6->fields[0]"
        )

        # Dump every parsed per-module Clang AST LLDB currently holds (main
        # plus all six dylibs), with no '--filter', exactly as a user
        # exploring a bug report would. This must not crash even though it
        # walks each module's own (still self-consistent) view of
        # 'Common' right after the six-way scratch-context merge above.
        self.expect("target modules dump ast")

        # Immediately follow with an unfiltered dump of the shared scratch
        # TypeSystem/ASTContext, which walks every Decl in the merged
        # context -- including the 'Common' RecordDecl that was just
        # reconciled from six mutually-conflicting array bounds. This is
        # the most promising place to look for corruption from the
        # six-way merge, and running it directly after the unfiltered
        # 'target modules dump ast' above stresses any global/static
        # caches shared between the two dump commands.
        self.expect("target dump typesystem", substrs=["Common"])
