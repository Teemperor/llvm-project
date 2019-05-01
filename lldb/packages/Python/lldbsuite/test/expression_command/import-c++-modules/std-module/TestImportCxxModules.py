"""
Test importing a C++ module and using its macros.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class ImportCxxModuleMacros(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    # FIXME: This should work on more setups, so remove these
    # skipIf's in the future.
    @skipIf(compiler=no_match("clang"))
    @skipIf(oslist=no_match(["linux"]))
    @skipIf(debug_info=no_match(["gmodules"]))
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))

        self.runCmd("log enable lldb expr")

        # Activate importing of std module.
        self.runCmd("settings set target.import-c++-modules true")
        # Call our macro.
        self.expect("expr add(2, 3)", substrs=['(int) $0 = 5'])
