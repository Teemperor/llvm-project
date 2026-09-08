"""
Test that calling a virtual method through an object whose class combines two
sibling classes that both virtually inherit the same abstract base (a
diamond) works, including when none of the classes declares its own
destructor (so each derived destructor is implicit, but still virtual since
it overrides the shared base's virtual destructor).

This used to crash the expression evaluator's Clang codegen while it built
the class's vtable: an implicit virtual destructor was not modeled as its
own member function, so Clang's VTableBuilder ended up treating the
diamond's shared virtual-base destructor slot as an unused duplicate in one
of the two paths reaching it, and asserted (MakeUnusedFunction refuses to
represent an "unused" destructor). See
other-bugs/lldb-diamond-vtable-unused-destructor.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestDiamondVirtualInheritanceDestructor(TestBase):
    def test(self):
        # TypeSystemClang hits an identical crash on this exact shape (a
        # pre-existing, out-of-scope Clang VTableBuilder issue that is not
        # fixed here -- see the README in
        # other-bugs/lldb-diamond-vtable-unused-destructor). A crash aborts
        # the whole dotest process, not just this test, so skip outright
        # rather than expectedFailure when TypeSystemClike is disabled.
        if not self.dbg.GetSetting(
            "symbols.enable-typesystem-clike"
        ).GetBooleanValue():
            self.skipTest(
                "TypeSystemClang crashes on this diamond-virtual-inheritance "
                "shape (VTableBuilder MakeUnusedFunction assert on an "
                "implicit virtual destructor); out of scope, not fixed here"
            )

        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("g_policy.weight()", result_type="int", result_value="3")
