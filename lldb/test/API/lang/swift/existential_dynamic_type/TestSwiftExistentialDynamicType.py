import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftExistentialDynamicType(lldbtest.TestBase):
    @skipEmbeddedSwift
    @swiftTest
    def test(self):
        """Test that type(of:) works on variables of existential type.

        Reconstructing the type of such a variable used to produce a bare
        protocol (constraint) type instead of an existential type, which made
        the Swift AST verifier abort while type-checking the expression."""

        self.build()
        filespec = lldb.SBFileSpec("main.swift")
        lldbutil.run_to_source_breakpoint(self, "break here", filespec)

        # With dynamic type resolution the existential is replaced by the
        # concrete type it holds.
        self.expect("expression -- type(of: single)", substrs=["a.S"])
        self.expect("expression -- type(of: composition)", substrs=["a.S"])
        self.expect("expression -- type(of: inherited)", substrs=["a.S"])
        self.expect("expression -- type(of: anything)", substrs=["Int"])

        # Without dynamic type resolution the static existential type is used.
        self.expect(
            "expression -d no-dynamic-values -- type(of: single)",
            substrs=["a.P1"],
        )
        self.expect(
            "expression -d no-dynamic-values -- type(of: composition)",
            substrs=["a.P1 & a.P2"],
        )
        self.expect(
            "expression -d no-dynamic-values -- type(of: inherited)",
            substrs=["a.Composed"],
        )
