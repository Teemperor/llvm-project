import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftArrayNestedTypealiasElement(lldbtest.TestBase):
    @skipEmbeddedSwift
    @swiftTest
    def test(self):
        """Format an array whose element type is an unresolvable type alias"""
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, 'break here', lldb.SBFileSpec('main.swift'))

        # The element type of this array is the type alias
        # ConcreteContainer<Double>.Element, which cannot be resolved from the
        # debug info. The formatter recovers Double from the runtime metadata of
        # the array's storage instead of asserting.
        self.expect("target variable -- doubles",
                    substrs=['1 value', '[0] = 42.5'])
