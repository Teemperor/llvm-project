import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil


class TestCase(TestBase):
    @no_debug_info_test
    def test_dumping(self):
        """Tests dumping an empty and non-empty scratch AST."""
        self.build()
        self.createTestTarget()

        # Make sure DummyStruct is not in the scratch AST by default.
        self.expect(
            "target dump typesystem", matching=False, substrs=["struct DummyStruct"]
        )

        # Move DummyStruct into the scratch AST via the expression evaluator.
        # FIXME: Once there is an SB API for using variable paths on globals
        # then this should be done this way.
        self.expect_expr("s", result_type="DummyStruct")

        # This last check assumes an implementation detail of TypeSystemClang:
        # evaluating `expr s` deports (via the ASTImporter) a copy of DummyStruct
        # into the *scratch* AST, which is what `target dump typesystem` prints.
        # TypeSystemClike has no ASTImporter; by design the expression result maps
        # back onto the DummyStruct type owned by the *module's* TypeSystemClike
        # (to preserve lazy completion -- see ClangTypeConverter::
        # ConvertViaReverseMap), so nothing is ever copied into the scratch
        # TypeSystemClike and it correctly stays empty. Skip this scratch-AST
        # assertion under TypeSystemClike.
        if self.dbg.GetSetting("symbols.enable-typesystem-clike").GetBooleanValue():
            self.skipTest(
                "TypeSystemClike does not deport expression result types into the "
                "scratch type system (no ASTImporter)"
            )

        # Dump the scratch AST and make sure DummyStruct is in there.
        self.expect("target dump typesystem", substrs=["struct DummyStruct"])

    @no_debug_info_test
    def test_invalid_arg(self):
        """Test an invalid invocation on 'target dump typesystem'."""
        self.build()
        self.createTestTarget()
        self.expect(
            "target dump typesystem arg",
            error=True,
            substrs=["'target dump typesystem' doesn't take any arguments."],
        )
