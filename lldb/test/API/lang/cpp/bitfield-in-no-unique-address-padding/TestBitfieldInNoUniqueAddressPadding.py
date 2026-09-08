"""
Regression test: a bitfield placed in the tail padding of a preceding
`[[no_unique_address]]` member. DWARF cannot spell that attribute, so LLDB
infers it from the offsets and re-describes the preceding member as
potentially-overlapping -- but the inference used to run for plain fields only,
leaving a following bitfield to be handed to Clang's expression-evaluator
codegen inside a member that still had its full sizeof worth of storage, which
asserts in CGRecordLayoutBuilder.cpp (checkBitfieldClipping, "Bitfield access
unit is not clipped"). See main.cpp and the fuzzer finding it came from,
cpp-test/findings/crash-seed427096881-prog019.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestBitfieldInNoUniqueAddressPadding(TestBase):
    def test(self):
        # TypeSystemClang has the identical blind spot (this is a generic LLDB
        # bug, not a TypeSystemClike-only one) and crashes the expression
        # evaluator's Clang codegen outright on this input, exactly as
        # TypeSystemClike did before this fix. Fixing that is out of scope
        # here, and matches how the sibling bugs in this family were handled
        # (see TestMultipleNonEmptyBaseBitfieldGap.py); avoid running the
        # crashing expressions under it, since a crash would kill the whole
        # test process, which is much worse than a skip.
        if not self.dbg.GetSetting(
            "symbols.enable-typesystem-clike"
        ).GetBooleanValue():
            self.skipTest(
                "TypeSystemClang still crashes on this input; only "
                "TypeSystemClike is fixed"
            )

        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        # The bitfields in `in`'s reused tail padding, and the member after
        # them: reading any of these used to crash the expression evaluator
        # while laying `Outer` out.
        self.expect_expr("g_outer.b", result_type="unsigned int", result_value="3")
        self.expect_expr("g_outer.c", result_type="unsigned int", result_value="7")
        self.expect_expr("g_outer.t", result_type="int", result_value="42")

        # The potentially-overlapping member itself must still be readable --
        # the fix marks it `[[no_unique_address]]` rather than dropping it or
        # the bitfield.
        self.expect_expr("g_outer.in.i", result_type="int", result_value="1")
        self.expect_expr("g_outer.in.c", result_type="char", result_value="'x'")

        # `frame variable` uses the DIL evaluator by default; exercise the
        # TypeSystemClike frame-variable path directly too (see
        # TestCppSmokeTest.py for the same pattern).
        self.runCmd("settings set target.experimental.use-DIL false")
        self.expect_var_path("g_outer.b", type="unsigned int:3", value="3")
        self.expect_var_path("g_outer.c", type="unsigned int:5", value="7")
        self.expect_var_path("g_outer.t", type="int", value="42")
        self.expect_var_path("g_outer.in.i", type="int", value="1")
