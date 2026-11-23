"""Test the SBInitializationOptions API."""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *


class SBInitializationOptionsTestCase(TestBase):
    NO_DEBUG_INFO_TESTCASE = True

    @add_test_categories(["typesystem-clike"])
    def test_initialize(self):
        options = lldb.SBInitializationOptions()
        self.assertSuccess(lldb.SBDebugger.Initialize(options))
