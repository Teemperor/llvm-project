"""
Test that elaborated types (e.g. Clang's ElaboratedType or TemplateType sugar).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, "// break here",
                                          lldb.SBFileSpec("main.cpp"))

        # Test that a plain elaborated type is only in the display type name but
        # not in the full type name (which is what the formatters use).
        result = self.expect_expr("::Struct s; s", result_type="::Struct")
        self.assertEqual(result.GetTypeName(), "Struct")

        # Test the same for template types (that only act as sugar to better
        # show how the template was specified by the user).
        self.expect("expr --top-level -- template<typename T> struct $V {};")
        result = self.expect_expr("$V<::Struct> s; s",
                                  result_type="$V< ::Struct>")
        self.assertEqual(result.GetTypeName(), "$V<Struct>")

        # Add a formatter for a typedef type.
        self.expect("type format add 'Typedef' -f x")
        # Check that a variable where the actual type ("Typedef") matches our
        # formatter but the elaborated name ("::Typedef") doesn't match our
        # formatter. Elaborated types shouldn't affect the formatter system.
        self.expect_expr("::Typedef value = 3; value", result_value="0x00000003")
