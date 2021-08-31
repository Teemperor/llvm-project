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

        m_val = self.expect_expr("m", result_type="WithMember", result_children=[
            ValueCheck(value="1")
        ])
        # FIXME: The non-display name doesn't include the function, so users
        # can't actually match specific classes by their name. Either document
        # or fix this.
        self.assertEqual(m_val.GetType().GetName(), "WithMember")
        # Try accessing the type in the expression evaluator.
        self.expect_expr("m.i", result_type="int", result_value="1")

        self.expect_expr("anon", result_type="Anon", result_children=[
            ValueCheck(value="2")
        ])

        # Try a class that is only forward declared.
        self.expect_expr("fwd", result_type="Forward *")
        self.expect("fwd->i", error=True, substrs=[
            "couldn't read its memory"
        ])
        self.expect("expression -- *fwd", error=True, substrs=[
            "couldn't read its memory"
        ])

        # Try a class that has a name that matches a class in the global scope.
        self.expect_expr("fwd_conflict", result_type="ForwardConflict *")
        # FIXME: This pulls in the unrelated type with the same name from the
        # global scope. This should fail to parse.
        self.expect("expression -- *fwd_conflict", error=True, substrs=[
            "couldn't read its memory"
        ])
