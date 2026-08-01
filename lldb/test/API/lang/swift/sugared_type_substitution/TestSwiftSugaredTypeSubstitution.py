import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftSugaredTypeSubstitution(lldbtest.TestBase):
    @skipEmbeddedSwift
    @swiftTest
    def test(self):
        """Canonicalizing a sugared type must yield a canonical mangled name"""
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, 'break here', lldb.SBFileSpec('main.swift'))

        # Desugaring the [[Double]] in the key path's value type used to produce
        # a Type(Type(...)) demangle tree, which defeated the remangler's
        # structural substitution matching. The resulting mangled name was not
        # canonical, which tripped an assertion in CompilerType.
        self.expect("frame variable kp",
                    substrs=['(WritableKeyPath<S, [[Double]]>) kp'])
        self.expect("frame variable s",
                    substrs=['values = 1 value', '[0] = 1.5', '[1] = 2.5'])
