import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        self.build()
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))

        self.expect_expr("(intptr_t)&d2g.ID - (intptr_t)&d2g", result_value="12")
