"""
Test the diagnostics emitted by our embeded Clang instance that parses expressions.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil
from lldbsuite.test.decorators import *

class ExprDiagnosticsTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)

        self.main_source = "main.cpp"
        self.main_source_spec = lldb.SBFileSpec(self.main_source)

    def test_source_and_caret_printing(self):
        """Test that the source and caret positions LLDB prints are correct"""
        self.build()

        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(self,
                                          '// Break here', self.main_source_spec)
        frame = thread.GetFrameAtIndex(0)

        # Test that source/caret are at the right position.
        value = frame.EvaluateExpression("unknown_identifier")
        self.assertFalse(value.GetError().Success())
        # We should get a nice diagnostic with a caret pointing at the start of
        # the identifier.
        self.assertIn("\nunknown_identifier\n^\n", value.GetError().GetCString())
        self.assertIn("<user expression 0>:1:1", value.GetError().GetCString())

        # Same as above but with the identifier in the middle.
        value = frame.EvaluateExpression("1 + unknown_identifier  ")
        self.assertFalse(value.GetError().Success())
        self.assertIn("\n1 + unknown_identifier", value.GetError().GetCString())
        self.assertIn("\n    ^\n", value.GetError().GetCString())

        # Multiline expressions.
        value = frame.EvaluateExpression("int a = 0;\nfoobar +=1;\na")
        self.assertFalse(value.GetError().Success())
        # We should still get the right line information and caret position.
        self.assertIn("\nfoobar +=1;\n^\n", value.GetError().GetCString())
        # It's the second line of the user expression.
        self.assertIn("<user expression 2>:2:1", value.GetError().GetCString())

        # Top-level expressions.
        top_level_opts = lldb.SBExpressionOptions();
        top_level_opts.SetTopLevel(True)

        value = frame.EvaluateExpression("void foo(unknown_type x) {}", top_level_opts)
        self.assertFalse(value.GetError().Success())
        self.assertIn("\nvoid foo(unknown_type x) {}\n         ^\n", value.GetError().GetCString())
        # Top-level expressions might use a different wrapper code, but the file name should still
        # be the same.
        self.assertIn("<user expression 3>:1:10", value.GetError().GetCString())

        # Multiline top-level expressions.
        value = frame.EvaluateExpression("void x() {}\nvoid foo;", top_level_opts)
        self.assertFalse(value.GetError().Success())
        self.assertIn("\nvoid foo;\n     ^", value.GetError().GetCString())
        self.assertIn("<user expression 4>:2:6", value.GetError().GetCString())

        # Test that we render Clang's 'notes' correctly.
        value = frame.EvaluateExpression("struct SFoo{}; struct SFoo { int x; };", top_level_opts)
        self.assertFalse(value.GetError().Success())
        self.assertIn("<user expression 5>:1:8: previous definition is here\nstruct SFoo{}; struct SFoo { int x; };\n       ^\n", value.GetError().GetCString())

        # Redefine something that we defined in a user-expression. We should use the previous expression file name
        # for the original decl.
        value = frame.EvaluateExpression("struct Redef { double x; };", top_level_opts)
        value = frame.EvaluateExpression("struct Redef { float y; };", top_level_opts)
        self.assertFalse(value.GetError().Success())
        self.assertIn("error: <user expression 7>:1:8: redefinition of 'Redef'\nstruct Redef { float y; };\n       ^\n", value.GetError().GetCString())
        self.assertIn("<user expression 6>:1:8: previous definition is here\nstruct Redef { double x; };\n       ^", value.GetError().GetCString())

    @skipUnlessDarwin
    def test_source_locations_from_objc_modules(self):
        self.build()

        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(self,
                                          '// Break here', self.main_source_spec)
        frame = thread.GetFrameAtIndex(0)

        # Import foundation so that the Obj-C module is loaded (which contains source locations
        # that can be used by LLDB).
        self.runCmd("expr @import Foundation")
        value = frame.EvaluateExpression("NSLog(1);")
        self.assertFalse(value.GetError().Success())
        # LLDB should print the source line that defines NSLog. To not rely on any
        # header paths/line numbers or the actual formatting of the Foundation headers, only look
        # for a few tokens in the output.
        # File path should come from Foundation framework.
        self.assertIn("/Foundation.framework/", value.GetError().GetCString())
        # The NSLog definition source line should be printed. Return value and
        # the first argument are probably stable enough that this test can check for them.
        self.assertIn("void NSLog(NSString *format", value.GetError().GetCString())

    def assert_has_main_file_diagnostic(self, error_msg):
        self.assertIn("diagnostics/main.cpp:1:1:", error_msg)
        self.assertIn("\n<content of", error_msg)
        self.assertIn("diagnostics/main.cpp>\n^\n", error_msg)

    def assert_has_not_main_file_diagnostic(self, error_msg):
        self.assertNotIn("diagnostics/main.cpp:1:1:", error_msg)
        self.assertNotIn("\n<content of", error_msg)
        self.assertNotIn("diagnostics/main.cpp>\n^\n", error_msg)

    def test_source_locations_from_debug_information(self):
        """Test that the source locations from debug information are correct"""
        self.build()

        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(self,
                                          '// Break here', self.main_source_spec)
        frame = thread.GetFrameAtIndex(0)
        top_level_opts = lldb.SBExpressionOptions();
        top_level_opts.SetTopLevel(True)

        # Test source locations of functions.
        value = frame.EvaluateExpression("foo(1, 2)")
        self.assertFalse(value.GetError().Success(), value.GetError().GetCString())
        self.assertIn(":1:1: no matching function for call to 'foo'\nfoo(1, 2)\n^~~",
                      value.GetError().GetCString())
        self.assert_has_main_file_diagnostic(value.GetError().GetCString())

        # Test source locations of records.
        value = frame.EvaluateExpression("struct FooBar { double x; };", top_level_opts)
        self.assertFalse(value.GetError().Success(), value.GetError().GetCString())
        self.assertIn(":1:8: redefinition of 'FooBar'\nstruct FooBar { double x; };\n",
                      value.GetError().GetCString())
        self.assert_has_main_file_diagnostic(value.GetError().GetCString())

        # Test source locations of enums.
        value = frame.EvaluateExpression("enum class EnumInSource {};", top_level_opts)
        self.assertFalse(value.GetError().Success(), value.GetError().GetCString())
        self.assert_has_main_file_diagnostic(value.GetError().GetCString())

    @skipIf(debug_info=no_match("dsym"),
            bugnumber="Template function decl can only be found via dsym")
    def test_source_locations_from_debug_information_templates(self):
        """Test that the source locations from debug information are correct
        for template functions"""
        self.build()

        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(self,
                                          '// Break here', self.main_source_spec)
        frame = thread.GetFrameAtIndex(0)

        # Test source locations of template functions.
        value = frame.EvaluateExpression("TemplateFunc<int>(1)")
        self.assertFalse(value.GetError().Success(), value.GetError().GetCString())
        self.assert_has_main_file_diagnostic(value.GetError().GetCString())

    def test_disabled_source_locations(self):
        """Test that disabling source locations with use-source-locations is
        actually disabling the creation of valid source locations"""
        self.build()
        # Disable source locations.
        self.runCmd("settings set plugin.symbol-file.dwarf.use-source-locations false")

        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(self,
                                          '// Break here', self.main_source_spec)
        frame = thread.GetFrameAtIndex(0)
        top_level_opts = lldb.SBExpressionOptions();
        top_level_opts.SetTopLevel(True)

        # Functions shouldn't have source locations now.
        value = frame.EvaluateExpression("foo(1, 2)")
        self.assertFalse(value.GetError().Success(), value.GetError().GetCString())
        self.assertIn(":1:1: no matching function for call to 'foo'\nfoo(1, 2)\n^~~",
                      value.GetError().GetCString())
        self.assert_has_not_main_file_diagnostic(value.GetError().GetCString())

        # Records shouldn't have source locations now.
        value = frame.EvaluateExpression("struct FooBar { double x; };", top_level_opts)
        self.assertFalse(value.GetError().Success(), value.GetError().GetCString())
        self.assertIn(":1:8: redefinition of 'FooBar'\nstruct FooBar { double x; };\n",
                      value.GetError().GetCString())
        self.assert_has_not_main_file_diagnostic(value.GetError().GetCString())

        # Enums shouldn't have source locations now.
        value = frame.EvaluateExpression("enum class EnumInSource {};", top_level_opts)
        self.assertFalse(value.GetError().Success(), value.GetError().GetCString())
        self.assert_has_not_main_file_diagnostic(value.GetError().GetCString())

        # Template functions shouldn't have source locations now.
        value = frame.EvaluateExpression("TemplateFunc<int>(1)")
        self.assertFalse(value.GetError().Success(), value.GetError().GetCString())
        self.assert_has_not_main_file_diagnostic(value.GetError().GetCString())
