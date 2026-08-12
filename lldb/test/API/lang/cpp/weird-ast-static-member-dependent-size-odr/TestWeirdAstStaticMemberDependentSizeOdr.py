import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstStaticMemberDependentSizeOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when the same class name ('Table') is
        defined in the main executable and in a dylib with an
        identically spelled static const int member 'kSize' and an
        identically spelled array member 'int data[kSize]', but the
        *value* of 'kSize' differs between the two modules (4 in the
        executable, 64 in the dylib). Because the array bound of
        'data' depends on the value of 'kSize' rather than just its
        name/type, the two (textually identical) declarations of
        'Table' actually describe completely different memory layouts
        and overall sizes.

        If LLDB's ASTImporter/TypeSystemClang machinery reuses or
        merges the static data member declaration for 'kSize' (or the
        RecordDecl for 'Table' itself) across modules without
        re-checking the array bound value it was originally completed
        with, it could end up producing an internally inconsistent
        RecordDecl: e.g. a 'Table' whose recorded size/layout doesn't
        match the array bound baked into its own 'data' member's
        type. Record layout and value formatting code downstream
        generally assumes such invariants hold, so this is a plausible
        way to trip an assertion or otherwise crash rather than just
        produce a wrong answer.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Evaluate each module's globals individually first.
        self.expect_expr("gMainTable.tag", result_value="1")
        self.expect_expr("gPluginTable.tag", result_value="2")

        self.expect_expr("Table::kSize", result_value="4")

        self.expect_expr("gMainTable.data[0]", result_value="10")
        self.expect_expr("gMainTable.data[3]", result_value="40")

        self.expect_expr("gPluginTable.data[0]", result_value="0")
        self.expect_expr("gPluginTable.data[10]", result_value="10")
        self.expect_expr("gPluginTable.data[63]", result_value="63")

        # Now evaluate an expression that references both modules'
        # (conflicting) 'Table' definitions together, forcing the
        # ASTImporter to import/complete both into the shared scratch
        # AST context at the same time.
        self.expect_expr("gMainTable.tag + gPluginTable.tag")

        # sizeof() on each module's notion of 'Table' should reflect
        # the differing array bounds baked into 'kSize' (or at least
        # not crash while computing/merging the record layout for the
        # conflicting decls).
        self.frame().EvaluateExpression("(int)sizeof(gMainTable)")
        self.frame().EvaluateExpression("(int)sizeof(gPluginTable)")
        self.frame().EvaluateExpression("(int)sizeof(Table)")

        # Also access/print the raw structs, which forces LLDB to
        # materialize and format their (conflicting) layouts,
        # including indexing into the 'data' array member itself.
        self.expect_expr("gMainTable")
        self.expect_expr("gPluginTable")
