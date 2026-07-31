import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        # Exercise the TypeSystemClike "frame variable" path directly instead of
        # the DIL evaluator.
        self.runCmd("settings set target.experimental.use-DIL false")
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        # Records, inheritance and pointers.
        self.expect_var_path("outer.m.i", type="int", value="4")
        self.expect_var_path("outer.x", type="long", value="-22")
        self.expect_var_path("ptr->x", type="long", value="-22")
        self.expect_var_path("ptr", type="Outer *")
        self.expect_var_path("*ptr", type="Outer")

        # Reference types: lvalue/rvalue references, references to records.
        # Like pointers, a reference's value is the referent's address; the
        # referenced value shows up in the summary instead.
        self.expect_var_path("ref", type="int &")
        self.expect_var_path("rref", type="int &&")
        self.expect_var_path("outer_ref", type="Outer &")
        # References are transparent: members are reachable directly.
        self.expect_var_path("outer_ref.m.i", type="int", value="4")
        self.expect_var_path("outer_ref.x", type="long", value="-22")

        # Class templates: the instantiation is a normal record type whose name
        # includes the template arguments.
        self.expect_var_path("wrapper", type="Wrapper<int>")
        self.expect_var_path("wrapper.value", type="int", value="7")
        self.expect_var_path("wrapper.tag", type="int", value="-1")

        # Non-type (value) template parameters: the instantiation name embeds
        # the value and the class is otherwise a normal record.
        self.expect_var_path("fixed", type="FixedArray<3>")
        self.expect_var_path("fixed.size", type="int", value="3")
        self.expect_var_path("fixed.data[1]", type="int", value="20")

        # Typedefs keep their alias name but behave like the aliased type.
        self.expect_var_path("my_int", type="MyInt", value="55")

        # const-qualified types.
        self.expect_var_path("const_int", type="const int", value="42")

        # Enumerations: values are shown as their enumerator names.
        self.expect_var_path("color", type="Color", value="Green")
        self.expect_var_path("fruit", type="Fruit", value="Banana")

        # Unions: all members share the same storage (offset 0).
        self.expect_var_path("number", type="Number")
        self.expect_var_path("number.i", type="int", value="65")

        # Standard-library containers. These generate large, deeply-nested
        # template types (typedefs, pointers, base classes, unions, bitfields,
        # template arguments and nested types), so exercising their data
        # formatters end-to-end validates broad type-system coverage.
        self.expect_var_path("str", type="std::string", summary='"hello"')
        self.expect_var_path("vec", type="std::vector<int>", summary="size=3")
        self.expect_var_path("vec[0]", type="int", value="10")
        self.expect_var_path("vec[2]", type="int", value="30")
        self.expect_var_path("tree_map", type="std::map<int, int>", summary="size=2")
        self.expect_var_path(
            "hash_map", type="std::unordered_map<int, int>", summary="size=1"
        )
