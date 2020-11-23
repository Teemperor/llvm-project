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

        self.runCmd("expr --top-level -- template<typename T> struct TL; template<> struct TL<int>; template<typename T> struct TL { int m; }; template<> struct TL<int> { int m; };")
        self.runCmd("expr --top-level -- template<typename T> struct TL; template<> struct TL<int>; template<typename T> struct TL { int m; }; template<> struct TL<int> { int m; };")
