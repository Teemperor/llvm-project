# TestSwiftGenericMethodInGenericType.py
#
# This source file is part of the Swift.org open source project
#
# Copyright (c) 2014 - 2016 Apple Inc. and the Swift project authors
# Licensed under Apache License v2.0 with Runtime Library Exception
#
# See https://swift.org/LICENSE.txt for license information
# See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
#
# ------------------------------------------------------------------------------
"""
Test expressions in a generic method of a generic type. The generic parameters
of the method itself cannot be evaluated unbound, so the expression evaluator
has to bind the generic parameters of the frame.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftGenericMethodInGenericType(TestBase):
    @skipEmbeddedSwift
    @swiftTest
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.swift")
        )
        # 'other' has the type of the method's own generic parameter, which
        # lives at depth 1. Printing the object description evaluates a second
        # expression that refers to the persistent result of the first one.
        self.expect("expression -O -- other", substrs=["hello"])
        self.expect("expression -- other", substrs=["(String) ", '= "hello"'])
        # 'value' has the type of the outermost generic parameter, at depth 0.
        self.expect("expression -- value", substrs=["(Int) ", "= 23"])
        self.expect("expression -O -- self", substrs=["value", "23"])
