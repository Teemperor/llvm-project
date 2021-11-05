import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @no_debug_info_test
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.cpp"))

        size_1 = self.expect_expr("class_with_size_1", result_type="ClassWithSize")
        size_8 = self.expect_expr("class_with_size_8", result_type="ClassWithSize")

        self.assertEqual(size_1.size, 1)
        self.assertEqual(size_8.size, 8)
