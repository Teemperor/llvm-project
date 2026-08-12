import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstEnumFixedUnderlyingOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when the same enum name ('Mode'), with the
        same enumerator names (A, B, C), is defined with a conflicting
        *fixed underlying type* in the main executable ('short', 2 bytes)
        and in a dylib ('long long', 8 bytes).

        This is a very literal, low-level ODR violation: it isn't just
        that the enumerator values differ (as in odr-enum-mismatch), but
        that the bit-width/storage size of the type itself differs
        between the two definitions. When LLDB's ASTImporter is asked to
        reconcile these two conflicting EnumDecls for 'Mode' inside the
        shared scratch AST context (because an expression references both
        modules' globals together), it may produce - or reuse - a Type
        node whose reported size/underlying type doesn't match what the
        value-reading code (which relies on the size of the *other*
        module's definition) expects. That mismatch is a plausible way to
        make LLDB read/format the wrong number of bytes for a scalar
        value, or otherwise crash, rather than just print a merely wrong
        answer.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Evaluate both globals individually first: each module's 'Mode'
        # should be read back with its own (module-local) notion of the
        # underlying type/size.
        self.expect_expr("gMainMode", result_type="Mode", result_value="B")
        self.expect_expr("gPluginMode", result_type="Mode", result_value="C")
        self.expect_expr("*gPluginModePtr", result_type="Mode", result_value="C")

        # sizeof() on each module's notion of 'Mode' should reflect the
        # differing fixed underlying types (short vs. long long) - or at
        # least not crash while resolving the conflicting merged decl's
        # size.
        self.expect_expr("(int)sizeof(gMainMode)", result_value="2")
        self.expect_expr("(int)sizeof(gPluginMode)", result_value="8")

        # Now evaluate expressions that reference both modules'
        # (conflicting) 'Mode' definitions together, forcing the
        # ASTImporter to import/complete both into the shared scratch AST
        # context at the same time.
        self.expect_expr(
            "(long long)gMainMode + (long long)gPluginMode", result_value="3"
        )

        # Cast a value that only makes sense for the 8-byte dylib
        # definition through the (merged/conflicting) 'Mode' type and
        # back, and print it. If the ASTImporter ends up conflating the
        # two definitions' underlying types, this is where a truncated
        # read (short-sized read of an 8-byte enum, or vice versa) would
        # show up as a crash or a garbage value.
        self.expect_expr(
            "(Mode)(long long)gPluginMode", result_type="Mode", result_value="C"
        )
        self.expect_expr("(Mode)(short)gMainMode", result_type="Mode", result_value="B")

        # Print both raw enum globals together, and via pointer, to force
        # LLDB's formatters/value objects to materialize both conflicting
        # 'Mode' definitions side by side.
        self.expect_expr("gMainMode")
        self.expect_expr("gPluginMode")
        self.expect_expr("gPluginModePtr")
