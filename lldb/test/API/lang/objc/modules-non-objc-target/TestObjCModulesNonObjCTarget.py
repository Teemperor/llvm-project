"""
Tests that importing ObjC modules in a non-ObjC target doesn't crash LLDB.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.c")
        )

        # TypeSystemCpp intentionally doesn't consume the clang::Decls produced
        # by clang @import modules (ClangModulesDeclVendor), so the imported
        # ObjC types/methods (e.g. NSString's +stringWithFormat:) aren't visible
        # to the expression and the expected CFStringCreateWithBytes rewrite path
        # is never reached. This is a known-unsupported feature bucket.
        if self.dbg.GetSetting(
            "symbols.enable-typesystem-cpp"
        ).GetBooleanValue():
            self.skipTest("@import clang modules not supported by TypeSystemCpp")

        # Import foundation to get some ObjC types.
        self.expect("expr --lang objc -- @import Foundation")
        # Do something with NSString (which requires special handling when
        # preparing to run in the target). The expression most likely can't
        # be prepared to run in the target but it should at least not crash LLDB.
        self.expect(
            'expr --lang objc -- [NSString stringWithFormat:@"%d", 1];',
            error=True,
            substrs=[
                "Rewriting an Objective-C constant string requires CFStringCreateWithBytes"
            ],
        )
