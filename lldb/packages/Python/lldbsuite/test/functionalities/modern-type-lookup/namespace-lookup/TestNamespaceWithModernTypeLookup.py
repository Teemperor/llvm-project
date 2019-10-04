from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class NamespaceWithModernTypeLookup(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        self.build()

        # Activate modern-type-lookup.
        self.runCmd("settings set target.experimental.use-modern-type-lookup true")

        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))

        # Test lookup in namespace.
        self.expect("expr A::i", substrs=["(int) ", " = 42\n"])
