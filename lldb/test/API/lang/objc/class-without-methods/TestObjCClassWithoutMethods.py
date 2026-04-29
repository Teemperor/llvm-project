import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        self.build_and_run()
        self.expect("frame var -P1 p", substrs=["_x = 0", "_y = 0"])
