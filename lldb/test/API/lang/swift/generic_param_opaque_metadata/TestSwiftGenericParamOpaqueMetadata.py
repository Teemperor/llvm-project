import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftGenericParamOpaqueMetadata(lldbtest.TestBase):
    @skipEmbeddedSwift
    @swiftTest
    def test(self):
        """Test binding a generic parameter whose metadata is opaque.

        The metadata pointer of a generic parameter is untrusted input: at the
        entry site of a generic function it hasn't been initialized yet and
        contains stale data. The reflection reader represents metadata it can't
        interpret as an opaque type, and remangling the resulting
        `Box<opaque>` demangle tree used to abort LLDB, because the remangler
        requires an OpaqueType node to have at least three children."""

        self.build()
        lldbutil.run_to_source_breakpoint(self, "break here",
                                          lldb.SBFileSpec("main.swift"))

        # Point the metadata pointer of Box's generic parameter at a metadata
        # record with an opaque kind.
        fake_metadata = self.target().FindFirstGlobalVariable(
            "fakeOpaqueMetadata")
        self.assertTrue(fake_metadata.IsValid())
        metadata_ptr = self.frame().FindVariable("$τ_0_0")
        self.assertTrue(metadata_ptr.IsValid())
        self.assertTrue(metadata_ptr.SetValueFromCString(
            hex(fake_metadata.GetLoadAddress())))

        # An opaque type cannot be mangled, so binding the generic parameter
        # fails and the unbound static type is used.
        self.expect("frame variable self", substrs=["(a.Box<T>) self"])
