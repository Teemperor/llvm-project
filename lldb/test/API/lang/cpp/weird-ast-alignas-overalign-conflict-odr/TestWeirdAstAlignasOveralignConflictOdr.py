"""
Test LLDB's handling of an alignas-driven over-alignment ODR conflict on
the same struct tag name ('Aligned') spread across the main executable
and two dylibs.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstAlignasOveralignConflictOdrTestCase(TestBase):
    def test_within_single_layout(self):
        """
        Using only the main executable's/DylibA's shared layout of
        'Aligned' (alignas(64), single 'int' field) is unambiguous and
        works fine, including pointer arithmetic that depends on
        sizeof(Aligned) being 64.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("sizeof(Aligned)", result_value="64")
        self.expect_expr("alignof(Aligned)", result_value="64")

        # Pointer subtraction between elements of the array relies on
        # ASTContext::getTypeSizeInChars() for this (64-byte) layout of
        # 'Aligned'.
        self.expect_expr("&arr[1] - &arr[0]", result_value="1")

        # DylibB's accessor can still be called and its 'x' field (which
        # happens to have the same name/type/offset in both conflicting
        # layouts) read without forcing a layout reconciliation.
        self.expect_expr("dylibB_get().x", result_value="100")

    def test_combined_layouts_via_accessor(self):
        """
        Same idea as test_within_single_layout, but the pointer-arithmetic
        result (which depends on sizeof(Aligned) as seen from the main
        executable's/DylibA's alignas(64) layout) is combined, in a single
        expression, with a read of the '.y' field that only exists on
        DylibB's plain, two-field layout of the very same tag name
        'Aligned'. Because the '.y' access here goes through
        dylibB_get()'s statically-typed return value (rather than through
        a locally-declared variable of the ambiguous name 'Aligned'),
        LLDB does not need to reconcile/convert between the two
        conflicting RecordDecls to evaluate this, and it succeeds.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr(
            "(&arr[1] - &arr[0]) + dylibB_get().y",
            result_type="long",
            result_value="201",
        )

    @expectedFailureAll(
        bugnumber="ASTImporter/TypeSystemClang treat the alignas(64), "
        "single-field 'Aligned' (main executable/DylibA) and the plain, "
        "two-field 'Aligned' (DylibB) as genuinely distinct C++ types: "
        "declaring a local variable typed 'Aligned' (which resolves to "
        "the alignas(64) layout already used in the enclosing "
        "expression) and initializing it from DylibB's differently-"
        "laid-out 'Aligned' fails overload resolution ('no viable "
        "conversion'), and the local's '.y' field access on top of that "
        "then also fails to find a member 'y' on the alignas(64) layout, "
        "so the two conflicting layouts of the same tag name can never "
        "be reconciled into a single merged type usable across both"
    )
    def test_combined_layouts_via_local(self):
        """
        Tests LLDB's expression evaluator when the same tag name
        'Aligned' has two mechanically incompatible definitions used
        together in one expression:
          - main executable / DylibA: struct alignas(64) Aligned { int x; };
            (over-aligned to a 64-byte cache line, sizeof == alignof == 64)
          - DylibB: struct Aligned { int x; long y; };
            (naturally aligned, sizeof == 16 on LP64 targets)

        This declares a local variable of the ambiguous name 'Aligned'
        (binding to the alignas(64) layout already pulled in by 'arr'),
        initializes it from DylibB's differently-shaped 'Aligned&', and
        combines the pointer-subtraction result (which depends on
        sizeof(Aligned) for the alignas(64) layout) with a '.y' field
        access on the local -- forcing LLDB to reconcile the two
        conflicting RecordDecls for 'Aligned' within one expression.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr(
            "Aligned local = dylibB_get(); (&arr[1] - &arr[0]) + local.y",
            result_type="long",
            result_value="201",
        )
