"""
Tests that SBValue::GetByteSize returns the runtime size of variable-length
arrays.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil


class TestVLAByteSize(TestBase):
    @skipIf(compiler="clang", compiler_version=["<", "8.0"])
    def test(self):
        self.build()
        _, process, _, _ = lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.c")
        )

        def check(a, b):
            frame = process.GetSelectedThread().GetFrameAtIndex(0)
            int_size = frame.FindVariable("a").GetByteSize()

            vla = frame.FindVariable("vla")
            self.assertEqual(vla.GetNumChildren(), a)
            self.assertEqual(vla.GetByteSize(), a * int_size)

            vla_2d = frame.FindVariable("vla_2d")
            self.assertEqual(vla_2d.GetByteSize(), a * b * int_size)

            vla_mixed = frame.FindVariable("vla_mixed")
            self.assertEqual(vla_mixed.GetByteSize(), a * 3 * int_size)

        check(2, 3)
        process.Continue()
        check(4, 5)
