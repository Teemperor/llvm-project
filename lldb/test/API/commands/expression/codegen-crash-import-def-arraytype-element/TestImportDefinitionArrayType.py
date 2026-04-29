import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestImportDefinitionArrayType(TestBase):
    def test(self):
        self.build_and_run()

        self.expect_expr("__private->o", result_type="char", result_value="'A'")
