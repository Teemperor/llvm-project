# encoding: utf-8
"""
Test lldb data formatter subsystem.
"""


import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

from ObjCDataFormatterTestCase import ObjCDataFormatterTestCase


class ObjCDataFormatterNSError(ObjCDataFormatterTestCase):
    SHARED_BUILD_TESTCASE = False

    NSERROR_RUNTIME_RECONSTRUCTION_BUGNUMBER = (
        "NSError's `_userInfo` ivar has no debug info (it's a private "
        "Foundation ivar), so TypeSystemClike reconstructs NSError's class "
        "layout from the ObjC runtime's type-encoding strings instead "
        "(TypeSystemClike::CreateRuntimeObjCInterface). That reconstruction "
        "can't express a real object graph: `id`/`Class`/`SEL` ivars become "
        "opaque untyped pointers (TypeSystemClike::RealizeObjCEncoding), so "
        "`_userInfo` shows as a raw pointer instead of running the "
        "NSDictionary formatter on it. This is a known, narrow gap (not "
        "something TypeSystemClang needs, since it has no such reconstruction "
        "step for a value it can otherwise treat as generic `id`)."
    )

    @add_test_categories(["typesystem-clike"])
    @skipIf(
        typesystem_clike="clike",
        bugnumber=NSERROR_RUNTIME_RECONSTRUCTION_BUGNUMBER,
    )
    def test_nserror_with_run_command(self):
        """Test formatters for NSError."""
        self.appkit_tester_impl(self.nserror_data_formatter_commands, True)

    @requireDarwin
    @add_test_categories(["typesystem-clike"])
    @skipIf(
        typesystem_clike="clike",
        bugnumber=NSERROR_RUNTIME_RECONSTRUCTION_BUGNUMBER,
    )
    def test_nserror_with_run_command_no_const(self):
        """Test formatters for NSError."""
        self.appkit_tester_impl(self.nserror_data_formatter_commands, False)

    def nserror_data_formatter_commands(self):
        self.expect(
            "frame variable nserror", substrs=['domain: @"Foobar" - code: -1234']
        )

        # The NSError data formatter will not apply to an NSError** after
        # https://github.com/llvm/llvm-project/pull/138209
        # "Add --pointer-match-depth option to type summary add command."
        #
        # Decide if we'll update the objc formatters to 
        # dereference an additional depth level to keep this
        # test working, or if we should drop the behavior.
        #self.expect(
        #    "frame variable nserrorptr", substrs=['domain: @"Foobar" - code: -1234']
        #)

        self.expect("frame variable nserror->_userInfo", substrs=["2 key/value pairs"])

        self.expect(
            "frame variable nserror->_userInfo --ptr-depth 1 -d run-target",
            substrs=['@"a"', "1", '@"b"', "2"],
        )
