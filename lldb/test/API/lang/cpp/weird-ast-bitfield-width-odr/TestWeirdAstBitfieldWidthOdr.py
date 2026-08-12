import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstBitfieldWidthOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when the same struct name ('Flags') is
        defined with conflicting bitfield layouts in the main executable
        and in a dylib. Both definitions have a same-named bitfield 'x',
        but its width is 3 bits in the executable and 29 bits in the
        dylib, and the surrounding bitfields (different names/widths in
        each module) shift the overall byte layout and size of 'Flags'
        substantially between the two definitions.

        When LLDB's ASTImporter merges/completes these conflicting
        definitions inside the shared scratch AST context, Clang's
        RecordLayoutBuilder (which has intricate, order-dependent
        bitfield packing logic) may be asked to lay out a Frankenstein
        RecordDecl with contradictory bit-widths for the same field
        name. This is a plausible way to trip an internal invariant
        check (or otherwise produce a bogus/crashing layout) rather
        than just a wrong answer.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Evaluate both globals individually first.
        self.expect_expr("gMainFlags.x", result_value="2")
        self.expect_expr("gPluginFlags.x", result_value="123456789")

        # Now evaluate expressions that reference both modules'
        # (conflicting) 'Flags' definitions in the same expression, which
        # forces the ASTImporter to import/complete both into the shared
        # scratch AST context together.
        self.expect_expr("gMainFlags.x + gPluginFlags.x")

        # sizeof() on each module's notion of 'Flags' should reflect the
        # differing byte layouts (or at least not crash while computing
        # the record layout for the conflicting merged decl).
        self.frame().EvaluateExpression("(int)sizeof(gMainFlags)")
        self.frame().EvaluateExpression("(int)sizeof(gPluginFlags)")

        # Also access the raw structs to force LLDB to materialize/copy
        # their (conflicting) layouts.
        self.expect_expr("gMainFlags")
        self.expect_expr("gPluginFlags")
