import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstAnonUnionOdrTestCase(TestBase):
    def test(self):
        """
        Puts LLDB's ASTImporter into a weird state by giving the same
        struct ("Variant") two conflicting definitions across the main
        executable and a dylib. Both definitions contain an *anonymous*
        union as a direct member, but the anonymous unions themselves
        disagree on their set of members (main.cpp: {int i; float f;},
        plugin.cpp: {int i; float f; double d; char buf[16];}), and
        therefore on size.

        Anonymous unions are surfaced to Clang's Sema/AST as
        IndirectFieldDecls injected into the enclosing RecordDecl's scope
        (so that `variant.i` resolves without naming the union). When the
        expression evaluator needs a single 'Variant' type that spans both
        the main executable and the dylib, the ASTImporter has to import
        and/or complete both RecordDecls into LLDB's scratch AST context.
        Because the two 'Variant' definitions are structurally different,
        this exercises the ASTImporter's ODR/conflict-handling logic together
        with the IndirectFieldDecl reconstruction for the anonymous union -
        which has previously been a source of crashes when the merged
        RecordDecls disagree on shape. This test doesn't assert a specific
        outcome; it primarily makes sure LLDB survives evaluating these
        expressions instead of crashing.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Access members of the anonymous union through both globals.
        self.expect_expr("main_variant.i", result_value="42")
        self.expect_expr("plugin_variant.tag", result_value="1")

        # Combine the two conflicting 'Variant' definitions in one
        # expression to force LLDB to reconcile both into the same AST.
        self.expect_expr("main_variant.tag + plugin_variant.tag", result_value="1")

        # sizeof(Variant) is ambiguous between the two definitions; just
        # make sure evaluating it doesn't crash LLDB.
        self.expect("expression sizeof(Variant)")

        # Access the float member of the anonymous union via the small
        # (main executable) definition.
        self.expect("expression main_variant.f")
