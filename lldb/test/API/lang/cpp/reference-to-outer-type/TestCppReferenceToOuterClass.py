import unittest
import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    @unittest.expectedFailure  # The fix for this was reverted due to llvm.org/PR52257
    def test(self):
        # PR52257 only affects TypeSystemClang; TypeSystemClike resolves the
        # nested type correctly and this test passes there. The xfail above is
        # TypeSystemClang-only, so skip under TypeSystemClike to avoid an
        # unexpected success.
        if self.dbg.GetSetting("symbols.enable-typesystem-clike").GetBooleanValue():
            self.skipTest("PR52257 does not affect TypeSystemClike (xfail is TypeSystemClang-only)")

        self.build()
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        test_var = self.expect_expr("test_var", result_type="In")
        nested_member = test_var.GetChildMemberWithName("NestedClassMember")
        self.assertEqual("Outer::NestedClass", nested_member.GetType().GetName())
