"""
Regression test: a class with two ordinary (non-virtual) polymorphic base
classes, neither of which is "nearly-empty" (each has its own real data
beyond its own vtable pointer, so there is no implicit-vtable-pointer-sharing
question at all -- contrast with
other-bugs/typesystemclang-virtual-primary-base-bitfield-gap/README.md, the
narrower sibling case this is not). An anonymous bitfield placed right after
the two bases (never recorded in DWARF at all, since compilers omit unnamed
bitfields) used to make LLDB's unnamed-bitfield-gap reconstruction refuse to
synthesize any padding for the gap at all, because the pre-existing safety
suppression for "this record has a base and we don't have a trustworthy
anchor" had no way to establish one from ordinary bases whose real extent is
nonetheless fully known from DWARF. That handed Clang's expression-evaluator
codegen a self-inconsistent layout that asserts in CGRecordLayoutBuilder.cpp.
See other-bugs/typesystemclang-virtual-primary-base-bitfield-gap/README.md
(the "two ordinary bases" sub-case documented there) and the torture-fuzzer
finding this was found from,
torture/findings/crash-seed377059235-prog016.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestMultipleNonEmptyBaseBitfieldGap(TestBase):
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
                "other-bugs/typesystemclang-virtual-primary-base-bitfield-gap); "
                "only TypeSystemClike is fixed"
            )

        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        # Calling the virtual methods exercises dynamic dispatch through both
        # bases' own (real, explicit) vtable pointers; the bitfield accesses
        # exercise the reconstructed unnamed-bitfield gap and everything
        # after it.
        self.expect_expr("g_derived.id()", result_type="int", result_value="1")
        self.expect_expr("g_derived.prio()", result_type="int", result_value="2")
        self.expect_expr("g_derived.ring_", result_type="unsigned int", result_value="3")
        self.expect_expr("g_derived.kernel_", result_type="unsigned int", result_value="0")
        self.expect_expr("g_derived.flags_", result_type="unsigned int", result_value="5")
        self.expect_expr("g_derived.tail", result_type="int", result_value="99")

        # `frame variable` uses the DIL evaluator by default; exercise the
        # TypeSystemClike frame-variable path directly too (see
        # TestCppSmokeTest.py for the same pattern).
        self.runCmd("settings set target.experimental.use-DIL false")
        self.expect_var_path("g_derived.ring_", type="unsigned int:2", value="3")
        self.expect_var_path("g_derived.kernel_", type="unsigned int:1", value="0")
        self.expect_var_path("g_derived.flags_", type="unsigned int:5", value="5")
        self.expect_var_path("g_derived.tail", type="int", value="99")
