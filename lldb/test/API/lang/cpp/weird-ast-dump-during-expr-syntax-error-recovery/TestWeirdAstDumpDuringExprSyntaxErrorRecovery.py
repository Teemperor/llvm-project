"""
Test LLDB's robustness when Clang's Sema error-recovery machinery is
deliberately triggered on a same-named type ('Point') that already has a
complete, well-formed definition in the target's debug info.

The scenario: at a breakpoint, 'struct Point { int x, y; };' is already
live (parsed from DWARF into LLDB's per-module Clang AST). We then
evaluate expressions at the debugger prompt that redeclare a *second*
'Point' with a deliberately malformed body/trailing expression (e.g. a
dangling '=' with no right-hand side), forcing Clang's Sema to bail out
via error recovery (RecoveryExpr / an incomplete or "being defined"
CXXRecordDecl) partway through parsing the redeclaration.

The concern motivating this test: LLDB reuses a single, persistent
"scratch" ASTContext across expression evaluations (for the ASTImporter
to merge declarations into). If Sema's error-recovery ever left a
partially-constructed, ill-formed 'Point' RecordDecl (fields with
sentinel/invalid QualTypes, or a decl stuck with isBeingDefined()==true)
injected into that persistent scratch context, a subsequent AST dump that
walks fields/records without checking for the invalid-type sentinel
could misbehave -- e.g. recurse into a null CanonicalType, or hit
'getAs<RecordType>()' on Clang's internal 'Dependent'/invalid placeholder
type, potentially reaching an llvm_unreachable in Clang's type-printing
code.

This test does not assert that such a crash happens (manual exploration
against the current LLDB found that it does not: LLDB discards the
per-expression Clang ASTContext entirely on a Sema/parse failure, so
nothing broken ever reaches the shared scratch ASTContext). Instead it
pins down the well-formed, non-corrupted behavior: each malformed
expression fails gracefully with a diagnostic, and immediately dumping
both the module AST ('target modules dump ast --filter Point') and the
scratch typesystem ('target dump typesystem') afterwards succeeds and
shows a clean, uncorrupted 'Point' -- so a future regression that leaks
broken error-recovery state into the scratch context would be caught by
a crash or a change in the dumped output.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpDuringExprSyntaxErrorRecoveryTestCase(TestBase):
    def test(self):
        """
        Repeatedly evaluate malformed expressions that redeclare 'Point'
        with a syntactically broken body while the debug-info 'Point' is
        already in scope, dumping the module AST and the scratch
        typesystem after each one, and confirm none of it crashes LLDB
        or corrupts the type's state.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # (1) A trailing incomplete assignment forces Sema to synthesize
        # a RecoveryExpr for the right-hand side of 'p.x = ' after having
        # already parsed a full, syntactically valid redeclaration of
        # 'struct Point { int x, y; };'. This is the primary scenario:
        # the *type* Point parses fine, but the statement using it does
        # not, so Sema's statement-level error recovery kicks in.
        self.expect(
            "expr struct Point { int x, y; }; Point p; p.x = ;",
            error=True,
            substrs=["expected expression"],
        )

        # Immediately after the failed expression returns its error,
        # make sure dumping both the module AST and the scratch
        # typesystem still works and shows an uncorrupted 'Point'.
        self.expect(
            "target modules dump ast --filter Point",
            substrs=["struct Point definition", "x", "y"],
        )
        self.expect("target dump typesystem")

        # (2) Repeat with a malformed member-initializer list, so the
        # error recovery triggers while Clang is still inside the
        # CXXRecordDecl's body (isBeingDefined() territory) rather than
        # in a statement after the type is already complete.
        self.expect(
            "expr struct Point { int x, y; Point(int a) : x(a) }; "
            "Point p(1); p.x",
            error=True,
        )
        self.expect(
            "target modules dump ast --filter Point",
            substrs=["struct Point definition", "x", "y"],
        )
        self.expect("target dump typesystem")

        # (3) Repeat using '--top-level' so the (failed) redeclaration
        # attempts to persist into the same scope as previous top-level
        # decls, then confirm a plain, well-formed use of 'Point' still
        # evaluates correctly afterwards -- i.e. none of the failed
        # attempts above left the persistent scratch ASTContext's notion
        # of 'Point' in a broken state.
        self.expect(
            "expr --top-level -- struct Point { int x, y; }; "
            "Point pTop; pTop.x = ;",
            error=True,
        )
        self.expect(
            "target modules dump ast --filter Point",
            substrs=["struct Point definition", "x", "y"],
        )

        self.expect_expr(
            "Point{3, 4}.x + Point{3, 4}.y", result_type="int", result_value="7"
        )

        # Final sanity check: after all of the above malformed
        # expressions, the module AST and scratch typesystem dumps still
        # succeed and still describe a clean, two-field 'Point'.
        self.expect(
            "target modules dump ast --filter Point",
            substrs=["struct Point definition", "x", "y"],
        )
        self.expect("target dump typesystem")
