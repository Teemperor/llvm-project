"""
Regression test: a class whose first dynamic base is a "nearly-empty"
polymorphic class (only a vtable pointer) implicitly shares that base's
vtable pointer under the Itanium C++ ABI (the base is selected as the class's
"primary base"), so clang emits no explicit `_vptr$Derived` DWARF member for
the derived class. An anonymous bitfield placed right after it (never
recorded in DWARF at all, since compilers omit unnamed bitfields) used to make
LLDB's unnamed-bitfield-gap reconstruction lose track of the first
pointer-width bits of the object entirely, handing Clang's expression-
evaluator codegen a self-inconsistent layout that asserts in
CGRecordLayoutBuilder.cpp. See
other-bugs/typesystemclang-virtual-primary-base-bitfield-gap/README.md.

Covers both a virtual and a non-virtual base -- Itanium's primary-base
selection picks a nearly-empty base as primary either way.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestVirtualPrimaryBaseBitfieldGap(TestBase):
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

        # Calling the virtual method through the implicitly-shared vtable
        # pointer exercises dynamic dispatch; the bitfield accesses exercise
        # the reconstructed unnamed-bitfield gap and everything after it.
        self.expect_expr("g_virtual.foo()", result_type="int", result_value="1")
        self.expect_expr("g_virtual.x", result_type="short", result_value="3")
        self.expect_expr("g_virtual.y", result_type="char", result_value="'\\x02'")

        self.expect_expr("g_nonvirtual.foo()", result_type="int", result_value="1")
        self.expect_expr("g_nonvirtual.x", result_type="short", result_value="3")
        self.expect_expr(
            "g_nonvirtual.y", result_type="char", result_value="'\\x03'"
        )
        self.expect_expr("g_nonvirtual.z", result_type="int", result_value="4")

        # `frame variable` uses the DIL evaluator by default; exercise the
        # TypeSystemClike frame-variable path directly too (see
        # TestCppSmokeTest.py for the same pattern).
        self.runCmd("settings set target.experimental.use-DIL false")
        self.expect_var_path("g_virtual.x", type="short:3", value="3")
        self.expect_var_path("g_virtual.y", type="char", value="'\\x02'")
        self.expect_var_path("g_nonvirtual.x", type="short:3", value="3")
        self.expect_var_path("g_nonvirtual.y", type="char", value="'\\x03'")
        self.expect_var_path("g_nonvirtual.z", type="int", value="4")
