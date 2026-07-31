import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        # Exercise the TypeSystemClike "frame variable" path directly instead of
        # the DIL evaluator.
        self.runCmd("settings set target.experimental.use-DIL false")
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.c")
        )

        self.expect_expr("outer.m.i", result_value="4")
