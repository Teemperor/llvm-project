from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    class_decl_kind = "CXXRecordDecl"
    struct_decl_kind = "CXXRecordDecl"

    struct_behind_ptr_decl = [struct_decl_kind, "StructBehindPointer"]
    struct_behind_ref_decl = [struct_decl_kind, "StructBehindRef"]
    struct_member_decl = [struct_decl_kind, "StructMember"]
    some_struct_decl = [struct_decl_kind, "SomeStruct"]
    other_struct_decl = [struct_decl_kind, "OtherStruct"]
    class_in_namespace_decl = [struct_decl_kind, "ClassInNamespace"]

    def get_ast_dump(self):
        res = lldb.SBCommandReturnObject()
        self.dbg.GetCommandInterpreter().HandleCommand('target modules dump ast', res)
        self.assertTrue(res.Succeeded())
        return res.GetOutput()

    def assert_decl_loaded(self, decl):
        decl_kind = "-" + decl[0] + " "
        decl_name = " " + decl[1]
        ast = self.get_ast_dump()
        found = False
        for line in ast.splitlines():
          if decl_kind in line and decl_name in line:
            found = True
        self.assertTrue(found, "Failed to find '" + decl_kind + "' '" + decl_name + "':\n" + ast)

    def assert_decl_not_loaded(self, decl, allow_incomplete=False):
        decl_kind = "-" + decl[0] + " "
        decl_name = " " + decl[1]
        ast = self.get_ast_dump()
        found = False
        for line in ast.splitlines():
          found_decl = decl_kind in line and decl_name in line
          if found_decl and allow_incomplete and "<undeserialized declarations>" in line:
            found_decl = False
          self.assertFalse(found_decl,
              "Unexpected loaded decl " + line + " in AST:\n" + ast)

    def setUp(self):
        TestBase.setUp(self)
        self.build()

    def clean_setup(self):
        lldbutil.run_to_source_breakpoint(self,
            "// Set break point at this line.", lldb.SBFileSpec("main.cpp"))
        self.assert_decl_not_loaded(self.struct_behind_ptr_decl)
        self.assert_decl_not_loaded(self.struct_behind_ref_decl)
        self.assert_decl_not_loaded(self.struct_member_decl)
        self.assert_decl_not_loaded(self.some_struct_decl)
        self.assert_decl_not_loaded(self.other_struct_decl)
        self.assert_decl_not_loaded(self.class_in_namespace_decl)

    def test_arithmetic_expression(self):
        self.clean_setup()

        self.expect("expr 1 + (int)2.0", substrs=['(int) $0'])
        self.assert_decl_not_loaded(self.some_struct_decl)
        self.assert_decl_not_loaded(self.other_struct_decl)

    def test_formatting_whole_struct(self):
        self.clean_setup()

        self.expect("expr struct_var", substrs=['(SomeStruct) $0'])
        self.assert_decl_loaded(self.some_struct_decl)
        self.assert_decl_loaded(self.struct_behind_ptr_decl)
        self.assert_decl_loaded(self.struct_behind_ref_decl)
        self.assert_decl_not_loaded(self.other_struct_decl, allow_incomplete=True)
        self.assert_decl_not_loaded(self.class_in_namespace_decl)

    def test_ref_of_struct(self):
        self.clean_setup()

        self.expect("expr &struct_var", substrs=['(SomeStruct *) $0'])
        self.assert_decl_loaded(self.some_struct_decl)
        self.assert_decl_loaded(self.struct_behind_ptr_decl)
        self.assert_decl_loaded(self.struct_behind_ref_decl)
        self.assert_decl_not_loaded(self.other_struct_decl, allow_incomplete=True)
        self.assert_decl_not_loaded(self.class_in_namespace_decl, allow_incomplete=True)
