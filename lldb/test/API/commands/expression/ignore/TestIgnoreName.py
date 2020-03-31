"""
Test calling user defined functions using expression evaluation.
This test checks that typesystem lookup works correctly for typedefs of
untagged structures.

Ticket: https://llvm.org/bugs/show_bug.cgi?id=26790
"""


import lldb

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestIgnoreName(TestBase):
    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        """Test that we can evaluate an expression that finds something inside
           a namespace that uses an Objective C keyword.
        """
        self.build()
        exe_path = self.getBuildArtifact("a.out")
        error = lldb.SBError()
        triple = None
        platform = None
        add_dependent_modules = False
        target = self.dbg.CreateTarget(exe_path, triple, platform,
                                       add_dependent_modules, error)
        self.assertTrue(error.Success(), "Make sure our target got created")
        expr_result = target.EvaluateExpression("a::Class a; a")
        self.assertTrue(expr_result.GetError().Success(),
                        "Make sure our expression evaluated without errors")
        self.assertTrue(expr_result.GetValue() == None,
                        'Expression value is None')
        self.assertTrue(expr_result.GetType().GetName() == "a::Class",
                        'Expression result type is "a::Class"')
