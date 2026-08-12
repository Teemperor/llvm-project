"""
Test LLDB's handling of an ODR violation where 'explicit' is on a
*different* member of the same class name in two different binaries: the
main executable's 'Meters' has an explicit constructor (implicit
conversion operator), while the dylib's 'Meters' has an implicit
constructor (explicit conversion operator).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstExplicitVsImplicitConversionOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each 'Meters' definition behaves correctly according to its own
        (different) placement of 'explicit', as long as the *other*
        conflicting definition hasn't already been imported into the
        shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # main.cpp's Meters: explicit constructor, implicit conversion
        # operator -> "double d = meters_from_main;" is well-formed.
        self.expect_expr("double d = meters_from_main", result_value="3")

        # plugin.cpp's Meters: implicit constructor, explicit conversion
        # operator -> "double d = meters_from_dylib;" is ill-formed, only
        # an explicit cast/direct call works.
        self.expect(
            "expr double d = meters_from_dylib",
            error=True,
            substrs=["no viable conversion from 'Meters' to 'double'"],
        )
        self.expect_expr(
            "static_cast<double>(meters_from_dylib)", result_value="5"
        )

    @expectedFailureAll(
        bugnumber="Once two ODR-conflicting definitions of the same class "
        "(here: 'explicit' swapped between the converting constructor and "
        "the conversion operator) have both been imported into the shared "
        "per-target scratch AST context via persistent expression result "
        "variables, a subsequent implicit Meters -> double conversion that "
        "is well-formed per the main executable's definition (non-explicit "
        "operator double()) is incorrectly rejected by Sema as having 'no "
        "viable conversion', even though the exact same conversion "
        "succeeded before the conflicting dylib definition was imported "
        "and even though explicitly calling operator double() on the same "
        "object still works fine"
    )
    def test_both_together(self):
        """
        Tests LLDB's behavior once *both* conflicting 'Meters' definitions
        have been imported/merged into the scratch AST context (by way of
        two persistent expression result variables, one of each type).

        Sema::PerformImplicitConversion's user-defined-conversion search
        filters out explicit conversion functions except in direct-init
        contexts. This test checks whether that filtering still gives the
        right answer once the merged 'Meters' RecordDecl in the scratch
        context holds conversion-function/constructor decls whose
        'explicit'-ness may have become confused between the two
        conflicting TU's versions.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Import the main executable's Meters (explicit ctor, implicit
        # conversion operator) into a persistent expression variable.
        self.expect_expr("Meters $p1 = meters_from_main")

        # Import the dylib's conflicting Meters (implicit ctor, explicit
        # conversion operator) into a second persistent expression
        # variable. This is the point where LLDB's DWARFASTParserClang/
        # ASTImporter machinery has to reconcile two simultaneously live,
        # incompatible definitions of the same tag name in the scratch
        # AST context (see `target dump typesystem`).
        self.expect_expr("Meters $p2 = meters_from_dylib")

        # This implicit conversion is well-formed according to
        # main.cpp's Meters (non-explicit operator double()); the same
        # conversion on the underlying global ('meters_from_main' rather
        # than the persisted '$p1') still succeeds fine at this point (see
        # test_each_alone). Ideally it should still succeed for '$p1' too
        # now that the dylib's conflicting Meters has also been imported
        # into the same scratch AST context.
        self.expect_expr("double d1 = $p1", result_value="3")
