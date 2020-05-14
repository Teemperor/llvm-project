"""
Tests that LLDB searches namespaces across all modules in the correct order
when resolving variables and functions.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def common_setup(self, build_single_exe):
        if build_single_exe:
          self.build(dictionary={"BUILD_SINGLE_EXE":1})
        else:
          self.build()

        # Make sure our test shared libraries can be found.
        self.ld_launch_info = lldb.SBLaunchInfo(None)
        self.ld_launch_info.SetWorkingDirectory(self.getBuildDir())
        env_expr = self.platformContext.shlib_environment_var + "=" + self.getBuildDir()
        self.ld_launch_info.SetEnvironmentEntries([env_expr], True)

    def common_tests(self, closest_tu):
        """
        Tests shared with all other tests. 'closest_tu' is the filename of
        the closest tu*.cpp file (i.e., "tu0.cpp" or "tu1.cpp") that LLDB
        should prefer when resolving variables.
        """
        closest_tu = '"' + closest_tu + '"'
        # Make sure LLDB can find 'Foo::VarNotInMain' in the other TUs.
        self.expect_expr("::Foo::VarNotInMain", result_summary=closest_tu)
        self.expect_expr("Foo::VarNotInMain", result_summary=closest_tu)

        # Find the variable that is only in the main.cpp TU.
        self.expect_expr("::Foo::VarOnlyInMain", result_summary="\"only-main.cpp\"")
        self.expect_expr("Foo::VarOnlyInMain", result_summary="\"only-main.cpp\"")

        # Make sure LLDB still finds all functions in TUs to correctly resolve overloaded functions.
        # In tu0 and tu1 there is an overload.
        self.expect_expr("Foo::OverloadedInlineFunction(1)", result_summary=closest_tu)
        # Only in main.cpp
        self.expect_expr("Foo::OverloadedInlineFunction(1, 2)", result_summary="\"main.cpp\"")

    def test_stopped_in_main_shared(self):
        """ Tests lookup when stopped in main function with shared libraries"""
        self.common_setup(build_single_exe=False)
        lldbutil.run_to_source_breakpoint(self, "// break in main", lldb.SBFileSpec("main.cpp"), launch_info=self.ld_launch_info)
        self.common_tests("tu0.cpp")

        # Make sure LLDB prefers the variable in the current file.
        self.expect_expr("::Foo::VarAlsoInMain", result_summary="\"also-main.cpp\"")
        self.expect_expr("Foo::VarAlsoInMain", result_summary="\"also-main.cpp\"")

    def test_stopped_in_shared_lib(self):
        """ Tests lookup when stopped in tu1 with shared libraries"""
        self.common_setup(build_single_exe=False)
        # First stop in the main executable to make sure the shared library is loaded.
        lldbutil.run_to_source_breakpoint(self, "// break before shared", lldb.SBFileSpec("main.cpp"), launch_info=self.ld_launch_info)
        # Now set the breakpoint in the shared library and continue there.
        breakpoint = self.target().BreakpointCreateBySourceRegex(
            "// break in other tu", lldb.SBFileSpec("tu1.cpp"))
        self.assertTrue(breakpoint.IsValid())
        stopped_threads = lldbutil.continue_to_breakpoint(self.process(), breakpoint)
        self.assertEqual(len(stopped_threads), 1)

        self.common_tests("tu1.cpp")

        # Make sure LLDB can find 'Foo::VarAlsoInMain' from the current shared library.
        self.expect_expr("::Foo::VarAlsoInMain", result_summary="\"tu1.cpp\"")
        self.expect_expr("Foo::VarAlsoInMain", result_summary="\"tu1.cpp\"")

    def test_stopped_in_main(self):
        """ Tests lookup when stopped in main function without shared libaries"""
        self.common_setup(build_single_exe=True)
        lldbutil.run_to_source_breakpoint(self, "// break in main", lldb.SBFileSpec("main.cpp"))
        self.common_tests("tu0.cpp")

        # Make sure LLDB prefers the variable in the current file.
        self.expect_expr("::Foo::VarAlsoInMain", result_summary="\"also-main.cpp\"")
        self.expect_expr("Foo::VarAlsoInMain", result_summary="\"also-main.cpp\"")

    def test_stopped_in_other_tu(self):
        """ Tests lookup when stopped in tu1 without shared libaries"""
        self.common_setup(build_single_exe=True)
        lldbutil.run_to_source_breakpoint(self, "// break in other tu", lldb.SBFileSpec("tu1.cpp"))

        # FIXME: This finds always finds the tu0.cpp variables, but LLDB is stopped
        # in tu1.cpp and should find the variables there.
        self.common_tests("tu0.cpp") # FIXME: This should be tu1.cpp.

        # Make sure LLDB can find 'Foo::VarAlsoInMain' from the current shared library.
        # FIXME: This should find the tu1 variables instead.
        self.expect_expr("::Foo::VarAlsoInMain", result_summary="\"also-main.cpp\"")
        self.expect_expr("Foo::VarAlsoInMain", result_summary="\"also-main.cpp\"")
