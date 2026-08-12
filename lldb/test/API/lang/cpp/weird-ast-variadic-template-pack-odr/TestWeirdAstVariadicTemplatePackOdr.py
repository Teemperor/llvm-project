import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstVariadicTemplatePackOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Tests LLDB's behaviour when the same variadic class template name
        ('Tuple3') is defined in the main executable and in a dylib, both
        instantiated with the exact same template-id ('Tuple3<int, int,
        int>'), but the pack expansion baked into the dependent array
        bound of member 'pad' is used differently in each module:
        'sizeof...(Ts)' (main.cpp, folds to 3) vs. 'sizeof...(Ts) + 1'
        (plugin.cpp, folds to 4). This is a genuine ODR violation: the
        *same* specialization has two incompatible definitions whose
        disagreement is baked into a dependent-sized array type's folded
        constant expression, rather than just a differing member list.

        Each module's global can be evaluated fine on its own, as long as
        the *other* conflicting specialization hasn't already been
        imported into the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("gMainTuple.a", result_type="int", result_value="1")
        self.expect_expr("gMainTuple.b", result_type="int", result_value="2")
        self.expect_expr("gMainTuple.c", result_type="int", result_value="3")
        self.expect_expr("(int)sizeof(gMainTuple.pad)", result_value="3")

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile two genuinely ODR-violating "
        "specializations of the same variadic class template (identical "
        "template-id, but a pack expansion in a dependent array bound that "
        "folds to different constants in each module): whichever "
        "specialization is imported into the scratch AST context first "
        "wins, and referring to the other module's global of that type "
        "afterwards fails with 'undeclared identifier'"
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same template specialization
        ('Tuple3<int, int, int>') has two incompatible definitions that
        differ only in how a pack expansion is folded into a dependent
        array bound: 'char pad[sizeof...(Ts)]' (byte size 3) in main.cpp
        vs. 'char pad[sizeof...(Ts) + 1]' (byte size 4) in the dylib.

        Since the pack size is baked into a dependent array type's folded
        constant expression, ODR-merging this forces
        Sema/ASTImporter/DWARFASTParserClang to reconcile two disagreeing
        instantiations of a dependent-sized array type for what is
        nominally the same instantiated specialization. Using both
        conflicting globals together in the same expression exercises
        this and shouldn't crash, even though LLDB currently can't
        actually reconcile the conflicting definitions.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Using both conflicting definitions of 'Tuple3<int, int, int>' in
        # the same expression shouldn't crash, and ideally should evaluate
        # correctly.
        self.expect_expr(
            "gMainTuple.a + gPluginTuple.a",
            result_type="int",
            result_value="11",
        )
