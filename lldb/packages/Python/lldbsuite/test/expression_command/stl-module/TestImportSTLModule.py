"""
Test some expressions involving STL data types.
"""

from __future__ import print_function

import sys
import unittest2
import os
import time
import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class STLTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def setUp(self):
        TestBase.setUp(self)
        self.source = 'main.cpp'
        self.line = line_number(
            self.source, '// Set break point at this line.')

#    @skipIf
#    @expectedFailureAll(bugnumber="llvm.org/PR36713")
    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    @skipIf(debug_info=no_match(["dwarf"]))
    def test(self):
        self.build(dictionary=self.getBuildFlags())
        exe = self.getBuildArtifact("a.out")

        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        lldbutil.run_break_set_by_file_and_line(
            self, "main.cpp", self.line, num_expected_locations=1, loc_exact=True)

        self.runCmd("run", RUN_SUCCEEDED)

        self.runCmd("settings set target.import-std-module true")
        self.expect("expr std::abs(-42)", substrs=['(int) $0 = 42'])
        self.expect("expr std::div(2, 1).quot", substrs=['(int) $1 = 2'])
