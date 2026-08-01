import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil

# swift::MetadataKind::Opaque
OPAQUE_METADATA_KIND = 0x300


class TestSwiftExistentialOpaqueMetadata(lldbtest.TestBase):
    def read_pointer(self, addr):
        error = lldb.SBError()
        data = self.process().ReadMemory(
            addr, self.process().GetAddressByteSize(), error)
        self.assertSuccess(error)
        return int.from_bytes(data, "little")

    def write_pointer(self, addr, value):
        error = lldb.SBError()
        self.process().WriteMemory(
            addr,
            value.to_bytes(self.process().GetAddressByteSize(), "little"),
            error)
        self.assertSuccess(error)

    @skipEmbeddedSwift
    @swiftTest
    def test(self):
        """Test an existential whose metadata is opaque.

        The metadata pointer of an existential container is untrusted input: in
        a function prologue it hasn't been initialized yet and holds stale data.
        The reflection reader represents metadata that it can't interpret as an
        opaque type, which cannot be remangled, so there is no dynamic type.
        Dynamic type resolution used to report success with an empty dynamic
        type anyway, and the empty type made the multi-word existential
        container be treated as a one-word scalar value. Extracting the
        container's fields out of that scalar then aborted LLDB."""

        self.build()
        lldbutil.run_to_source_breakpoint(self, "break here",
                                          lldb.SBFileSpec("main.swift"))

        container = self.frame().FindVariable("value").GetStaticValue()
        self.assertTrue(container.IsValid())
        ptr_size = self.process().GetAddressByteSize()
        # The metadata pointer follows the container's three payload words.
        metadata_slot = container.GetLoadAddress() + 3 * ptr_size

        # Fabricate a metadata record with an opaque kind, prefixed by the value
        # witness table of the container's real metadata.
        fake_metadata = self.target().FindFirstGlobalVariable("fakeMetadata")
        self.assertTrue(fake_metadata.IsValid())
        witness_table = self.read_pointer(
            self.read_pointer(metadata_slot) - ptr_size)
        self.write_pointer(
            fake_metadata.GetChildMemberWithName(
                "valueWitnessTable").GetLoadAddress(), witness_table)
        kind_addr = fake_metadata.GetChildMemberWithName("kind").GetLoadAddress()
        self.write_pointer(kind_addr, OPAQUE_METADATA_KIND)

        # Point the existential container at the fabricated metadata.
        self.write_pointer(metadata_slot, kind_addr)

        # An opaque type cannot be remangled, so no dynamic type is found and
        # the static type is used. All words of the container are still read
        # from the container's address; the first one holds the boxed Int.
        self.expect("frame variable value",
                    substrs=["(Any) value",
                             "payload_data_0 = 0x000000000000002a",
                             "payload_data_1 = 0x", "payload_data_2 = 0x"])
