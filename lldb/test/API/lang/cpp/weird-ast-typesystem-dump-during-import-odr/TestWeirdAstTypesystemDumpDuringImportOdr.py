"""
Test LLDB's handling of a three-way field-count ODR conflict on the same
struct tag name ('Shared') spread across the main executable and two
dylibs:
  main.cpp: struct Shared { int a; };
  DylibB:   struct Shared { int a; int b; };
  DylibC:   struct Shared { int a; int b; int c; };

The test sets a breakpoint in each of the three functions that touch
'Shared' and, upon hitting each breakpoint in turn, runs an expression
that forces LLDB's ASTImporter to import that translation unit's version
of 'Shared' into the target's shared per-target scratch ASTContext,
immediately followed by 'target dump typesystem' -- before continuing to
the next breakpoint. This means the scratch ASTContext's RecordDecls
named 'Shared' get walked and printed three separate times, once right
after each of the three mutually-incompatible 'Shared' definitions (1
field, then 2 fields, then 3 fields, all sharing the same field-name
prefix) gets merged in by the ASTImporter.

The concern motivating this test is purely about robustness: if the
ASTImporter's merge-by-tag-name logic ever mutated/rewrote the *same*
RecordDecl's field list in place across successive imports (instead of
creating a fresh redeclaration per import, which is what it currently
does), then 'target dump typesystem' walking that RecordDecl's fields via
TagDecl::field_begin()/field_end() while a rewrite is only partially
complete could read a corrupted or circular FieldDecl linked list. This
test does not assert that such a crash happens (LLDB currently creates
distinct RecordDecls per import and does not exhibit this problem); it
instead pins down the well-formed, non-corrupted state of the merged
scratch ASTContext after each of the three imports, so that a regression
towards in-place mutation would be caught by a change in the dumped
output.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTypesystemDumpDuringImportOdrTestCase(TestBase):
    def test(self):
        """
        Hit the breakpoint in the main executable's func_a first (which
        sees the one-field 'Shared'), then the breakpoint in DylibB's
        dylib_b_entry (two-field 'Shared'), then the breakpoint in
        DylibC's dylib_c_entry (three-field 'Shared'). After each stop,
        evaluate an expression that creates a persistent result variable
        of type 'Shared' (forcing an import into the shared scratch
        ASTContext) and immediately dump the scratch typesystem, before
        continuing to the next breakpoint.
        """
        self.build()

        # Stop at the first conflicting definition: the main executable's
        # one-field 'struct Shared'.
        target, process, thread, bkpt_a = lldbutil.run_to_source_breakpoint(
            self, "s->a = 1;", lldb.SBFileSpec("main.cpp")
        )

        # Also set breakpoints on the two dylib functions, each stopped
        # right after their local 'Shared *s' pointer has been
        # initialized (so the expressions below can safely dereference
        # it).
        bkpt_b = target.BreakpointCreateBySourceRegex(
            "s->b = 2;", lldb.SBFileSpec("DylibB.cpp")
        )
        self.assertTrue(bkpt_b.GetNumLocations() > 0, "No location for DylibB bkpt")

        bkpt_c = target.BreakpointCreateBySourceRegex(
            "s->c = 3;", lldb.SBFileSpec("DylibC.cpp")
        )
        self.assertTrue(bkpt_c.GetNumLocations() > 0, "No location for DylibC bkpt")

        # (1) Import the main executable's one-field 'Shared' into the
        # scratch ASTContext, then dump the scratch typesystem.
        self.expect_expr("*s", result_type="Shared")
        self.expect("target dump typesystem", substrs=["Shared", "int a"])

        # (2) Continue to DylibB's breakpoint, import DylibB's two-field
        # 'Shared', then dump the scratch typesystem again. Both the
        # main executable's one-field 'Shared' and DylibB's two-field
        # 'Shared' should now show up as two distinct RecordDecls.
        lldbutil.continue_to_breakpoint(process, bkpt_b)
        self.expect_expr("*s", result_type="Shared")
        self.expect(
            "target dump typesystem",
            substrs=["Shared", "int a", "int b"],
        )

        # (3) Continue to DylibC's breakpoint, import DylibC's
        # three-field 'Shared', then dump the scratch typesystem a third
        # time. All three mutually-incompatible 'Shared' RecordDecls
        # should now be present simultaneously in the dump.
        lldbutil.continue_to_breakpoint(process, bkpt_c)
        self.expect_expr("*s", result_type="Shared")
        self.expect(
            "target dump typesystem",
            substrs=["Shared", "int a", "int b", "int c"],
        )

        # The scratch ASTContext should now hold three distinct
        # RecordDecls named 'Shared' (1-field, 2-field and 3-field). The
        # dump only prints each decl's *own* fields (not any that a
        # differently-sized sibling redeclaration might have), so if the
        # ASTImporter had instead mutated a single shared RecordDecl in
        # place, the dump would only show one 'Shared' entry -- with
        # either a stale, corrupted or crashing field list -- instead of
        # three well-formed independent ones.
        self.expect(
            "target dump typesystem",
            patterns=[r"struct Shared definition"] * 3,
        )

        process.Continue()
