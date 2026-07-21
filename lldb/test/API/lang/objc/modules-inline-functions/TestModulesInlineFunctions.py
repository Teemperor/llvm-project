"""Test that inline functions from modules are imported correctly"""


import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class ModulesInlineFunctionsTestCase(TestBase):
    @add_test_categories(["gmodules"])
    @skipIf(macos_version=["<", "10.12"])
    def test_expr(self):
        self.build()
        if self.dbg.GetSetting(
            "symbols.enable-typesystem-cpp"
        ).GetBooleanValue():
            # TypeSystemCpp synthesizes a fresh Clang AST for expressions from
            # cpp_typesystem types and does not consume the clang::Decls produced
            # by ClangModulesDeclVendor, so `@import myModule` decls (isInline,
            # notInline) are not visible in the expression context.
            # This is the intentionally-unsupported clang `@import` modules bucket.
            self.skipTest("@import clang modules not supported by TypeSystemCpp")
        exe = self.getBuildArtifact("a.out")
        self.runCmd("file " + exe, CURRENT_EXECUTABLE_SET)

        # Break inside the foo function which takes a bar_ptr argument.
        lldbutil.run_to_source_breakpoint(
            self, "// Set breakpoint here.", lldb.SBFileSpec("main.m")
        )

        self.runCmd(
            'settings set target.clang-module-search-paths "'
            + self.getSourceDir()
            + '"'
        )

        self.expect(
            "expr @import myModule; 3",
            VARIABLES_DISPLAYED_CORRECTLY,
            substrs=["int", "3"],
        )

        self.expect("expr isInline(2)", VARIABLES_DISPLAYED_CORRECTLY, substrs=["4"])
