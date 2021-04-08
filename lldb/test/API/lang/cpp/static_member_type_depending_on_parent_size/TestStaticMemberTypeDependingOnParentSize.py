import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @no_debug_info_test
    def test(self):
        self.build()
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))

        self.expect("target variable r")
