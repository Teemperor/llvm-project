import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        # Exercise the TypeSystemCpp "frame variable" path directly instead of
        # the DIL evaluator.
        self.runCmd("settings set target.experimental.use-DIL false")
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        self.expect_var_path("outer.m.i", value="4")
        self.expect_var_path("outer.x", value="-22")
        self.expect_var_path("ptr->x", value="-22")
        self.expect_var_path("ptr", type="Outer *")
        self.expect_var_path("*ptr", type="Outer")
