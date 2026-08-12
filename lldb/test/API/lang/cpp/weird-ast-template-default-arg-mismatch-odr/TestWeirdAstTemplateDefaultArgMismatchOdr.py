"""
Test LLDB's handling of two class template specializations that share the
exact same *surface* spelling ('Grid<int>') across two different modules,
but which are actually different specializations under the hood because
the template's default argument for its second (non-type) parameter
differs between the main executable and the dylib.

Built with -gsimple-template-names (see Makefile), clang emits the
DW_TAG_structure_type DIE's DW_AT_name as the bare "Grid" in both object
files (no template arguments baked into the string at all): the actual
arguments are only recoverable by DWARFASTParserClang from the
DW_TAG_template_type_parameter / DW_TAG_template_value_parameter child
DIEs. This probes whether DWARFASTParserClang/the ASTImporter ever
conflates the two mutually-incompatible specializations ('Grid<int, 4>'
from main.cpp and 'Grid<int, 8>' from plugin.cpp) because their DWARF
"names" collapse to the same string.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTemplateDefaultArgMismatchOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's expression evaluator and 'target modules dump ast'
        against two globals whose types are both nominally 'Grid<int>' but
        are actually different specializations ('Grid<int, 4>' in
        main.cpp, 'Grid<int, 8>' in plugin.cpp) once the differing default
        template arguments are taken into account.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Each global should retain its own module's notion of 'Grid<int>'
        # (i.e., its own default argument for N), and the two should not
        # have been conflated into a single cached specialization.
        self.expect_expr(
            "sizeof(main_g) != sizeof(plugin_g)", result_type="bool", result_value="true"
        )

        # Read a field out of both (differently-sized) globals in a single
        # expression: this is the point where LLDB has to import both
        # conflicting 'Grid' specializations into the shared scratch AST
        # context at once.
        self.expect_expr(
            "main_g.cells[3] + plugin_g.cells[7]", result_type="int", result_value="16"
        )

        # Dumping the per-module ASTs (filtered to just 'Grid') should show
        # two distinct ClassTemplateSpecializationDecls -- one with
        # template argument '4' (from main.cpp) and one with '8' (from
        # plugin.cpp) -- rather than a single conflated entry.
        self.expect(
            "target modules dump ast --filter Grid",
            substrs=["TemplateArgument integral '4'", "TemplateArgument integral '8'"],
        )
