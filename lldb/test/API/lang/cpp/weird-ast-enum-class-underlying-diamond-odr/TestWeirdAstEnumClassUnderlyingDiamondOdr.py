"""
Test LLDB's handling of a three-way ODR conflict on the same enum tag
name ('Status') spread across three different dylibs, where the
conflict mixes *scoped vs. unscoped* enums on top of three different
underlying types.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstEnumClassUnderlyingDiamondOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's expression evaluator against three dylibs that each
        define an incompatible 'Status' enum:
          - DylibX: enum class Status : uint8_t { OK = 0, FAIL = 1 };  (scoped, 1 byte)
          - DylibY: enum class Status : int16_t { OK = 0, FAIL = 1 };  (scoped, 2 bytes)
          - DylibZ: enum        Status          { OK = 0, FAIL = 1 };  (unscoped, implicit int)

        This is a three-way ODR conflict that combines the documented
        tricky corner of ASTImporter enum merging (EnumDecl::isScoped()
        mismatch between DylibX/DylibY's scoped enum and DylibZ's
        unscoped enum) with a three-way underlying-type conflict
        (uint8_t vs. int16_t vs. int). The main executable links all
        three dylibs and only forward-declares each dylib's *_init()
        function; it never spells 'Status' itself, and instead calls
        through each dylib's own global function pointer (gGetX/gGetY/
        gGetZ, each only ever declared with its own module's 'Status'
        return type in debug info).

        Evaluating a single expression that reads all three globals and
        casts each result to 'int' forces LLDB's
        DWARFASTParserClang/ASTImporter machinery to import and reconcile
        three simultaneously-conflicting definitions of 'Status' - two
        scoped with different underlying types, and one unscoped - into
        the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "entry", lldb.SBFileSpec("main.cpp")
        )

        # Each dylib's own (module-local) notion of 'Status' should be
        # read back correctly on its own first.
        self.expect_expr("gGetX()", result_type="Status", result_value="FAIL")
        self.expect_expr("gGetY()", result_type="Status", result_value="OK")
        self.expect_expr("gGetZ()", result_type="Status", result_value="FAIL")

        # sizeof() on each module's notion of 'Status' should reflect its
        # own underlying type (1 byte, 2 bytes, 4 bytes respectively) -
        # or at least not crash while resolving the conflicting merged
        # decl's size.
        self.expect_expr("(int)sizeof(gGetX())", result_value="1")
        self.expect_expr("(int)sizeof(gGetY())", result_value="2")
        self.expect_expr("(int)sizeof(gGetZ())", result_value="4")

        # Now evaluate a single expression that references all three
        # (mutually conflicting) 'Status' definitions together, forcing
        # the ASTImporter to import/reconcile a scoped-vs-scoped-vs-
        # unscoped, three-way underlying-type conflict inside the shared
        # scratch AST context at once. This shouldn't crash, and ideally
        # should evaluate correctly (FAIL=1 + OK=0 + FAIL=1 == 2).
        self.expect_expr(
            "(int)gGetX() + (int)gGetY() + (int)gGetZ()", result_value="2"
        )

        # Dump the merged scratch AST state and each module's own AST for
        # 'Status'. Neither of these should crash, regardless of whatever
        # (possibly inconsistent) merged EnumDecl the expression above
        # left behind in the scratch context.
        self.expect("target dump typesystem", substrs=["scratch Clang type system"])
        self.expect("target modules dump ast --filter Status", substrs=["Status"])

        # Re-run the combined expression again now that a (possibly
        # already-merged/conflicting) 'Status' decl lives in the scratch
        # context, to see whether reusing it behaves any differently
        # from importing it fresh.
        self.expect_expr(
            "(int)gGetZ() + (int)gGetY() + (int)gGetX()", result_value="2"
        )
