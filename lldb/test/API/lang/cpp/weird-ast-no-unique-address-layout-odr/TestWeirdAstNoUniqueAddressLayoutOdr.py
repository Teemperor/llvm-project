import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstNoUniqueAddressLayoutOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Tests LLDB's behavior when a type with the same name ('Holder') is
        defined differently in two different modules: the main executable
        declares its empty 'e' member with [[no_unique_address]] (so
        'Holder' is 4 bytes, with 'x' at offset 0), while the dylib declares
        the exact same field list without that attribute (so 'Holder' is 8
        bytes, with 'x' at offset 4).

        This is a very concrete, low-level ODR violation (identical field
        names/types, but a mechanically different record layout because of
        an attribute difference) that stresses Clang's record layout
        computation -- specifically ASTContext::getASTRecordLayout's
        per-RecordDecl layout cache -- when LLDB's ASTImporter
        merges/completes the conflicting 'Holder' decls in the shared
        scratch AST context.

        Evaluated on its own (without referencing the other module's
        conflicting global), each global's field access should still
        produce the correct, unsurprising result.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("sizeof(g_main_holder)", result_value="4")
        self.expect_expr("g_main_holder.x", result_type="int", result_value="42")

        self.expect_expr("sizeof(g_dylib_holder)", result_value="8")
        self.expect_expr("g_dylib_holder.x", result_type="int", result_value="99")

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile two genuinely ODR-violating "
        "definitions of the same class ('Holder') that only differ by a "
        "[[no_unique_address]] attribute on a field: whichever definition's "
        "RecordDecl gets laid out first via "
        "ASTContext::getASTRecordLayout wins, and the other module's global "
        "of the identically-named type silently gets read back using that "
        "same (wrong) field offset for 'x', producing a well-formed but "
        "incorrect value instead of an error"
    )
    def test_both_together(self):
        """
        Using both conflicting definitions of 'Holder' in the same
        expression shouldn't crash, and ideally should evaluate correctly
        (42 + 99 == 141). In practice, only one of the two RecordDecls'
        layouts survives in the merged/scratch AST context, so reading
        '.x' off of whichever global was not used to establish that layout
        first returns a value read at the wrong byte offset.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "g_main_holder.x + g_dylib_holder.x",
            result_type="int",
            result_value="141",
        )
