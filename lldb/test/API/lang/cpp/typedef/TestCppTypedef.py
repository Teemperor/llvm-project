"""
Test typedef types.
"""

import lldb
import lldbsuite.test.lldbutil as lldbutil
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *


@skipIfWasm  # no expression evaluation
class TestCppTypedef(TestBase):
    def test_typedef(self):
        """
        Test that we retrieve typedefed types correctly
        """

        self.build()
        self.main_source_file = lldb.SBFileSpec("main.cpp")
        lldbutil.run_to_source_breakpoint(
            self, "Set a breakpoint here", lldb.SBFileSpec("main.cpp")
        )

        # First of all, check that we can get a typedefed type correctly in a simple case.
        expr_result = self.expect_expr(
            "(GlobalTypedef)s",
            result_type="GlobalTypedef",
            result_children=[ValueCheck(value="0.5")],
        )

        # The type should be a typedef.
        typedef_type = expr_result.GetType()
        self.assertTrue(typedef_type.IsValid())
        self.assertTrue(typedef_type.IsTypedefType())

        # The underlying type should be S<float>.
        typedefed_type = typedef_type.GetTypedefedType()
        self.assertTrue(typedefed_type.IsValid())
        self.assertEqual(typedefed_type.GetName(), "S<float>")

        # Check that we can get a typedefed type correctly in the case
        # when an elaborated type is created during the parsing
        expr_result = self.expect_expr(
            "(GlobalTypedef::V)s.value", result_type="GlobalTypedef::V"
        )

        # The type should be a typedef.
        typedef_type = expr_result.GetType()
        self.assertTrue(typedef_type.IsValid())
        self.assertTrue(typedef_type.IsTypedefType())

        # The underlying type should be float.
        typedefed_type = typedef_type.GetTypedefedType()
        self.assertTrue(typedefed_type.IsValid())
        self.assertEqual(typedefed_type.GetName(), "float")

        # Try accessing a typedef inside a namespace.
        self.expect_expr(
            "(ns::NamespaceTypedef)s", result_children=[ValueCheck(value="0.5")]
        )

        # Try accessing a typedef inside a struct/class.
        self.expect_expr(
            "(ST::StructTypedef)s", result_children=[ValueCheck(value="0.5")]
        )
        # A nested typedef is resolved directly from debug info, so it works
        # even without a local variable of that type in scope to inject it.
        self.expect_expr(
            "(NonLocalVarStruct::OtherStructTypedef)1", result_value="1"
        )

        # Check the generated Clang AST.
        self.filecheck("image dump ast a.out", __file__, "--strict-whitespace")


# The AST dump is record-centric: it emits the record definitions the module
# has produced (TypeSystemClike resolves typedefs and namespaces lazily rather
# than eagerly materializing standalone TypedefDecls/NamespaceDecls up front).
# CHECK:      {{^}}|-ClassTemplateSpecializationDecl {{.*}} struct S definition
# CHECK:      {{^}}|-CXXRecordDecl {{.*}} struct ST definition
# CHECK:      {{^}}`-CXXRecordDecl {{.*}} struct NonLocalVarStruct definition
