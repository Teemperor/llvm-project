"""
Regression test: LLDB's unnamed-bitfield-gap reconstruction (which fills in
the padding compilers omit DIEs for) used to round a preceding non-bitfield
member's end up to the next 32-bit word boundary before comparing it against
the next bitfield's real DWARF-given offset. That overshoots and skips
synthesizing padding entirely when the real gap is genuine but narrower than a
full word, handing Clang's expression-evaluator codegen a self-inconsistent
layout that asserts in CGRecordLayoutBuilder.cpp. See
other-bugs/typesystemclang-bitfield-gap-word-rounding/README.md.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestNonBitfieldBitfieldGapWordRounding(TestBase):
    def test(self):
        # TypeSystemClang has the identical blind spot (this is a generic
        # LLDB bug, not a TypeSystemClike-only one) and crashes the expression
        # evaluator's Clang codegen outright on this input -- see the README
        # referenced above. Fixing that is out of scope here; avoid running
        # the crashing expressions under it, since a crash would kill the
        # whole test process, which is much worse than a skip.
        if not self.dbg.GetSetting(
            "symbols.enable-typesystem-clike"
        ).GetBooleanValue():
            self.skipTest(
                "TypeSystemClang still crashes on this input (see "
                "other-bugs/typesystemclang-bitfield-gap-word-rounding); "
                "only TypeSystemClike is fixed"
            )

        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        # The narrow (genuinely < 1 word), non-word-aligned gap: this is the
        # shape that used to make the old word-rounding heuristic overshoot
        # past the real bitfield offset and skip padding synthesis entirely.
        self.expect_expr("g_narrow.a", result_type="char", result_value="'\\x01'")
        self.expect_expr("g_narrow.b", result_type="unsigned int", result_value="2")
        self.expect_expr("g_narrow.trigger", result_type="int", result_value="3")

        # The full-word-gap shape: a non-regression check that removing the
        # rounding didn't reopen a case where a genuinely word-aligned
        # bitfield needs its full gap accounted for.
        self.expect_expr("g_fullword.x", result_type="char", result_value="'\\x04'")
        self.expect_expr(
            "g_fullword.y", result_type="unsigned int", result_value="5"
        )
        self.expect_expr("g_fullword.trigger", result_type="int", result_value="6")

        # `frame variable` uses the DIL evaluator by default; exercise the
        # TypeSystemClike frame-variable path directly too (see
        # TestCppSmokeTest.py for the same pattern).
        self.runCmd("settings set target.experimental.use-DIL false")
        self.expect_var_path("g_narrow.a", type="char", value="'\\x01'")
        self.expect_var_path("g_narrow.b", type="unsigned int:3", value="2")
        self.expect_var_path("g_narrow.trigger", type="int", value="3")
        self.expect_var_path("g_fullword.x", type="char", value="'\\x04'")
        self.expect_var_path("g_fullword.y", type="unsigned int:32", value="5")
        self.expect_var_path("g_fullword.trigger", type="int", value="6")
