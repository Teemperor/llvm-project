"""
Test importing a C++ module and using its macros.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class ImportCxxModuleMacros(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["gmodules"])
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))

        # Activate importing of std module.
        self.runCmd("settings set target.import-c++-modules true")
        # Call our macro.
        self.expect("expr my_abs(-42)", substrs=['(int) $0 = 42'])
