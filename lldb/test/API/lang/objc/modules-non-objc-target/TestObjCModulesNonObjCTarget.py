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

        # In a non-Objective-C target the ObjC runtime path that TypeSystemClike
        # uses to resolve a class named by an expression (`NSString`) isn't
        # available, and TypeSystemClike only consults the ClangModulesDeclVendor
        # to *complete* an already-generated interface, not to look a class up
        # by name -- so `+stringWithFormat:` never resolves and the expected
        # CFStringCreateWithBytes rewrite path is never reached.
        if self.dbg.GetSetting(
            "symbols.enable-typesystem-clike"
        ).GetBooleanValue():
            self.skipTest("@import ObjC class name lookup in a non-ObjC target "
                          "not supported by TypeSystemClike")

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
