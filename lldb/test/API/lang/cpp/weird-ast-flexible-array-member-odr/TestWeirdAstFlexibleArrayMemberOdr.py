import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFlexibleArrayMemberOdrTestCase(TestBase):
    def test_dylib_buffer_alone(self):
        """
        The dylib-side 'Buffer' (with a concrete fixed-size trailing array)
        can be evaluated fine on its own, as long as the main-executable's
        conflicting 'Buffer' (with a GNU flexible array member trailing
        field) hasn't already been imported into the shared per-target
        scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("gFixedBuffer.len", result_value="8")
        self.expect_expr("gFixedBuffer.data[7]", result_value="8")

    def test_main_exe_buffer_alone(self):
        """
        The main-executable-side 'Buffer' (with a GNU flexible array
        member trailing field, making the type itself have an "unknown"
        size in the strict sense) can likewise be evaluated fine on its
        own, as long as the dylib's conflicting 'Buffer' hasn't already
        been imported into the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("gFlexBuffer->len", result_value="4")
        self.expect_expr("gFlexBuffer->data[2]", result_value="30")

    @expectedFailureAll(
        bugnumber="ASTImporter/TypeSystemClang can't reconcile two "
        "same-named RecordDecls where one has a GNU flexible array member "
        "trailing field (incomplete/'unknown' size) and the other has a "
        "concrete fixed-size trailing array: once both definitions of "
        "'Buffer' have been imported into the scratch AST context, any "
        "further name lookup for 'Buffer' fails with 'reference to "
        "'Buffer' is ambiguous' instead of a clean merge or a clean error"
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when the same-named 'Buffer' struct has a
        GNU flexible array member trailing field in the main executable,
        but a concrete fixed-size trailing array in a dylib. Both 'Buffer'
        RecordDecls have to be merged by the ASTImporter/TypeSystemClang
        machinery when evaluating expressions that reference globals from
        both modules, which is a layout/sizing contradiction that the
        record-completion code may not handle gracefully.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the dylib-side definition (fixed-size trailing array)
        # first.
        self.expect_expr("gFixedBuffer.len", result_value="8")

        # Now reference the main-executable-side definition (flexible
        # array member trailing field) in the same expression evaluation
        # session, forcing LLDB to merge the two conflicting 'Buffer'
        # RecordDecls.
        self.expect_expr("gFlexBuffer->len", result_value="4")
        self.expect_expr("gFlexBuffer->data[2]", result_value="30")

        # Finally combine both globals (and sizeof) in a single
        # expression to stress the merged-type layout computation.
        self.expect_expr(
            "gFixedBuffer.data[7] + gFlexBuffer->data[3] + (int)sizeof(Buffer)",
        )
