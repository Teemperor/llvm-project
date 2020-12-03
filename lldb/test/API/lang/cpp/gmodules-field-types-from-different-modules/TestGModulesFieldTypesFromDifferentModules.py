"""
Tests that a record with types from two modules works with -gmodules.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        self.build()
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.expect_expr("mix_modules.u1.c.i", result_type="int", result_value="0")
