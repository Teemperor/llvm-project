import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstUsingDeclTypeAliasOdrTestCase(TestBase):
    def test_each_member_alone(self):
        """
        Tests that accessing each global variable's own member (through its
        real DWARF type, not through the ambiguous unqualified 'Widget'
        alias) works correctly even though the main executable and the
        dylib both define an incompatible 'ns::Widget' and both pull it into
        file scope via 'using ns::Widget;'.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("gA.x", result_type="int", result_value="42")
        self.expect_expr("gB.y", result_type="double", result_value="1.5")

    @expectedFailureAll(
        bugnumber="The file-scope 'using ns::Widget;' declarations in "
        "main.cpp and plugin.cpp are not represented in DWARF (no "
        "DW_TAG_imported_declaration is emitted for them), so the plain "
        "unqualified name 'Widget' is not visible to the expression "
        "parser at all ('use of undeclared identifier'). Referring to the "
        "qualified name 'ns::Widget' instead does resolve, but LLDB's "
        "scratch AST context silently picks whichever of the two "
        "differently-laid-out 'ns::Widget' definitions was imported first, "
        "so 'sizeof(ns::Widget)' can silently report the wrong size "
        "(4, i.e. sizeof(int), instead of the dylib's 16-byte "
        "{double y; float z;} layout) depending on unrelated evaluation "
        "order -- a real but non-crashing ODR-merge bug."
    )
    def test_ambiguous_widget_name(self):
        """
        Tests LLDB's behavior when asked to resolve the ambiguous
        unqualified name 'Widget' (introduced via 'using ns::Widget;' at
        file scope) or the qualified name 'ns::Widget', once both
        conflicting definitions of 'ns::Widget' -- one with a single 'int x'
        member (main.cpp) and one with 'double y; float z;' members
        (plugin.cpp) -- have already been imported into the target's shared
        scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Import both conflicting 'ns::Widget' definitions into the scratch
        # AST context first.
        self.expect_expr("gA.x", result_type="int", result_value="42")
        self.expect_expr("gB.y", result_type="double", result_value="1.5")

        # The plain 'using'-introduced name isn't visible to the expression
        # parser (no debug info is emitted for the file-scope 'using'
        # declaration), and the qualified name resolves to whichever
        # definition happened to be imported first, giving the wrong size.
        self.expect_expr("sizeof(Widget)", result_type="unsigned long")
        self.expect_expr("sizeof(ns::Widget)", result_type="unsigned long", result_value="16")
