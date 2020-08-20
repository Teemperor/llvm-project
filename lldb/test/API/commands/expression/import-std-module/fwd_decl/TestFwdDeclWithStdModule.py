from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    def test_non_cpp_language(self):
        target = self.dbg.GetDummyTarget()

        #self.runCmd("settings set target.import-std-module true")

        # Declare a template class with a field that uses the template type
        # parameter.
        opts = lldb.SBExpressionOptions()
        opts.SetTopLevel(True)
        #result = target.EvaluateExpression("struct FwdDecl;", opts)
        # FIXME: This fails with "Couldn't find $__lldb_expr() in the module"
        # but it should succeed. The fact that this code has nothing to run
        # shouldn't be an error.
        # self.assertSuccess(result.GetError())

        # Instantiate and produce a value with that template as the type.
        # The field in the value will have a SubstTemplateTypeParam that
        # should behave like a normal field.
        result = target.EvaluateExpression("struct FwdDecl; FwdDecl *F = nullptr; F")
        self.assertSuccess(result.GetError())
        result.GetValue()
        self.assertFalse(True)
