import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstUsingEnumValueConflictOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each side's 'Color' variable can be evaluated fine on its own, as
        long as the *other* conflicting definition of 'colors::Color' hasn't
        already been imported into the shared per-target scratch AST
        context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("cA", result_type="colors::Color", result_value="Red")

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile two genuinely ODR-violating "
        "definitions of the same enum 'colors::Color' (different underlying "
        "types and different enumerator counts) pulled into scope via a "
        "using-declaration: whichever definition is imported into the "
        "scratch AST context first wins for unqualified/using-declaration "
        "lookup, so referencing an enumerator that only exists in the "
        "other definition fails with 'no member named ... in colors::Color' "
        "even though a debug-info-qualified lookup can still find both "
        "(unmerged) EnumDecls side by side in the scratch AST"
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same qualified enum name
        ('colors::Color'), pulled into the global namespace via a
        using-declaration in both the main executable and a dylib, has two
        incompatible definitions: one implicitly 'int'-backed with 3
        enumerators (main.cpp), and one explicitly 'char'-backed with 4
        enumerators (plugin.cpp, with the extra enumerator 'Alpha').

        Referencing 'Color::Alpha' (only defined in plugin.cpp's version) is
        expected to work, but the using-declaration currently in scope may
        resolve 'Color' to whichever 'colors::Color' definition got imported
        into the scratch AST context first, which need not be the one that
        actually has 'Alpha'.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Import both conflicting 'colors::Color' definitions into the
        # scratch AST context.
        self.expect_expr("cA", result_type="colors::Color", result_value="Red")
        self.expect_expr("cB", result_type="colors::Color", result_value="Alpha")

        # 'Alpha' only exists in plugin.cpp's definition of 'colors::Color'.
        # Referring to it through the using-declaration (which is in scope
        # via both main.cpp and plugin.cpp) should still find it.
        self.expect_expr("(int)Color::Alpha", result_type="int", result_value="3")
