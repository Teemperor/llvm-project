import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @no_debug_info_test
    def test(self):
        self.build()
        lldbutil.run_to_name_breakpoint(self, "main")

        # Use an identifier that can be resolved to multiple internal symbols.
        # This should warn the user about the fact that there are multiple
        # symbols. LLDB should actually reject the expression and not
        # randomly pick one of the symbols (which might not be the one the
        # user wants).
#        self.expect("expr global_var", error=True,
#                    substrs=["warning: Multiple internal symbols found for 'global_var'",
#                             "use of undeclared identifier 'global_var'"])

        # Define a local variable that has the same name as the symbols.
        # This should not be rejected just because they are random stray symbols
        # that have the same name.
 #       self.expect_expr("int global_var = 3; global_var", result_value="3")

        # Try to define a top-level global variable with the same name.
        self.runCmd("expr --top-level -- float global_var = 4;")

        self.expect_expr("global_var", result_value="4")
