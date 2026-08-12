import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstPackedAttributeOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behavior when a type with the same name ('Packed') is
        defined differently in two different modules: the main executable
        declares it with __attribute__((packed)) while the dylib declares
        the exact same field list without that attribute. This means the
        two RecordDecls named 'Packed' have mechanically different
        alignment/padding rules and therefore different sizes and member
        offsets, even though the source-level field lists look identical.

        This is a very concrete, low-level ODR violation (as opposed to a
        difference in the list of members) that stresses Clang's record
        layout computation when LLDB's ASTImporter merges/completes the
        conflicting 'Packed' decls in the shared scratch AST context. We
        evaluate sizeof(Packed) and an expression that combines the
        main executable's and the dylib's globals, hoping to trigger a
        crash (rather than just a wrong answer) somewhere in
        TypeSystemClang/DWARFASTParserClang/ASTImporter record-layout
        handling.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("sizeof(Packed)")

        self.expect_expr("g_main_packed.d")

        self.expect_expr("g_dylib_packed.d")

        self.expect_expr(
            "g_main_packed.d + g_dylib_packed.d + sizeof(g_main_packed) + sizeof(g_dylib_packed)"
        )
