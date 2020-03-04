"""
Tests that enabled logging does not break lldb. The rest of the test
suite runs without logging enabled so this test is supposed to touch
several different parts of LLDB to make sure the logging code is
not crashing or dead-locking in any way.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self,"// break here", lldb.SBFileSpec("main.cpp"))

        # Enable all lldb log channels just that we reach all the logging code.
        # FIXME: Enabling the dwarf logging deadlocks LLDB.
        self.runCmd("log enable lldb all -v");

        # Do some things that trigger generation of log statements.
        self.expect("expr Foo s;")
        self.expect("expr f.foo()")
        self.expect("expr f.m")
        self.expect("expr f.n")
        self.expect("expr f.n.x")
        self.expect("expr invalid", error=True)

        self.expect("expr $0")
