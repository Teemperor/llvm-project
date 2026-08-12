"""
Puts LLDB's ASTImporter/TypeSystemClang machinery into a weird, ODR-
violating state using a *typedef name swap* between two dylibs:

  - DylibA.h: struct RealA { int x; };  typedef RealA Alias;
  - DylibB.h: struct RealB { double y; int z; };  typedef RealB Alias;

Both dylibs use the exact same typedef name ('Alias') for two completely
unrelated, differently-sized RecordDecls. On top of that, each dylib also
carries a second typedef that names the *other* dylib's conflicting struct
via a forward declaration only:

  - DylibA.h additionally has: struct RealB; typedef RealB AliasB;
  - DylibB.h additionally has: struct RealA; typedef RealA AliasA;

so each dylib's debug info has a typedef whose underlying RecordDecl is
incomplete in that dylib, while the *same* RecordDecl is fully defined in
the other dylib. This is meant to stress the typedef-merging fast path in
ASTImporter (which compares the underlying canonical type of same-named
TypedefNameDecls) at a point where the target RecordDecl may still be
incomplete.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstCyclicTypedefNameSwapOdrTestCase(TestBase):
    def test_qualified_access(self):
        """
        Baseline: accessing each dylib's global through that dylib's own
        entry point (getA()/getB()) and its own struct tag name (RealA/
        RealB, rather than the ambiguous shared typedef name 'Alias')
        should always work, and should keep working even after LLDB has
        been made to look at the conflicting 'Alias' typedefs and their
        incomplete cross-referencing AliasA/AliasB counterparts.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Access both conflicting globals through their unambiguous tag
        # names first.
        self.expect_expr("((RealA *)getA())->x", result_value="1")
        self.expect_expr("((RealB *)getB())->y", result_value="2")

        # Force LLDB to resolve the two forward-only cross typedefs
        # (AliasA in dylib B pointing at an incomplete RealA there, and
        # AliasB in dylib A pointing at an incomplete RealB there). Both
        # calls return null, but evaluating them still forces
        # DWARFASTParserClang/ASTImporter to deal with the incomplete
        # RecordDecls behind these typedefs.
        self.expect_expr("getAliasBFromA() == nullptr", result_value="true")
        self.expect_expr("getAliasAFromB() == nullptr", result_value="true")

        # Dump the per-module ASTs for both dylibs filtered on the
        # conflicting names, and the merged scratch context. None of this
        # should crash LLDB, regardless of what has been evaluated so far.
        self.expect("target modules dump ast --filter Alias")
        self.expect("target modules dump ast --filter AliasA")
        self.expect("target modules dump ast --filter AliasB")
        self.expect("target dump typesystem")

        # Re-check the unambiguous accesses still work after all of the
        # above.
        self.expect_expr("((RealA *)getA())->x", result_value="1")
        self.expect_expr("((RealB *)getB())->y", result_value="2")

    @expectedFailureAll(
        bugnumber="unqualified 'Alias' is ambiguous across two dylibs with "
        "same-named, differently-shaped typedefs: whichever single "
        "'RealA'/'RealB' definition unqualified name lookup happens to "
        "settle on for 'Alias' gets used for *every* occurrence of "
        "'Alias' in the expression, so combining a dylib-A pointer and a "
        "dylib-B pointer through the shared name 'Alias' in one "
        "expression can never type-check for both of them at once"
    )
    def test_unqualified_alias_is_ambiguous(self):
        """
        Documents a real limitation: unqualified 'Alias' cannot mean "the
        right RealA/RealB depending on which pointer it's applied to". A
        single expression's use of the name 'Alias' resolves to exactly
        one of the two conflicting typedefs (Clang's own ambiguous-lookup
        diagnostic can also fire here, which is expected and correct:
        'Alias' really is ambiguous as a standalone name). This means an
        expression that applies 'Alias' to *both* dylib A's and dylib B's
        pointer in the same statement can never be made to type-check for
        both sides simultaneously: whichever of RealA/RealB 'Alias' picks,
        the access through the *other* pointer fails ("no member named
        'x' in 'RealB'" or "no member named 'y' in 'RealA'"). This is
        deliberately different from test_qualified_access, which shows
        the same globals are perfectly readable once accessed through
        their real, unambiguous tag names (RealA/RealB) instead of the
        shared typedef name.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Whichever of RealA/RealB unqualified 'Alias' resolves to here,
        # it is used for *both* casts below (a single expression only
        # gets to resolve 'Alias' once). So this can never evaluate to
        # 3 (1 + 2) the way the equivalent qualified accesses in
        # test_qualified_access do: either the whole expression fails to
        # compile as ambiguous, or it compiles against just one of
        # RealA/RealB and then the access through the *other* pointer
        # fails to find the member it's looking for.
        self.expect_expr(
            "(long)(*(Alias *)getA()).x + (long)(*(Alias *)getB()).y",
            result_type="long",
            result_value="3",
        )
