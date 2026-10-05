import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
from lldbsuite.test.gdbclientutils import *
from lldbsuite.test.lldbgdbclient import GDBRemoteTestBase


class TestMemoryRegionVMMapFields(GDBRemoteTestBase):
    def test_full_info(self):
        """
        A qMemoryRegionInfo reply containing all of the vmmap-parity keys
        should be reflected in the corresponding SBMemoryRegionInfo
        accessors.
        """

        class MyResponder(MockGDBServerResponder):
            def qHostInfo(self):
                return "ptrsize:8;endian:little;vm-page-size:4096;"

            def qMemoryRegionInfo(self, addr):
                return (
                    "start:1000;size:4000;permissions:rw;"
                    "max-permissions:rwx;"
                    "share-mode:2;"
                    "region-type:1e;"
                    "is-submap:0;"
                    "pages-resident:2;"
                    "pages-dirtied:1;"
                    "pages-swapped-out:0;"
                )

        self.server.responder = MyResponder()
        target = self.dbg.CreateTarget("")
        process = self.connect(target)
        lldbutil.expect_state_changes(
            self, self.dbg.GetListener(), process, [lldb.eStateStopped]
        )

        region = lldb.SBMemoryRegionInfo()
        err = process.GetMemoryRegionInfo(0x1000, region)
        self.assertSuccess(err)

        self.assertTrue(region.IsMaxReadable())
        self.assertTrue(region.IsMaxWritable())
        self.assertTrue(region.IsMaxExecutable())

        self.assertTrue(region.HasShareMode())
        self.assertEqual(region.GetShareMode(), lldb.eShareModePrivate)

        self.assertTrue(region.HasRegionTypeTag())
        self.assertEqual(region.GetRegionTypeTag(), 0x1E)

        self.assertFalse(region.IsSubmap())

        self.assertTrue(region.HasNumResidentPages())
        self.assertEqual(region.GetNumResidentPages(), 2)

        self.assertTrue(region.HasNumDirtiedPages())
        self.assertEqual(region.GetNumDirtiedPages(), 1)

        self.assertTrue(region.HasNumSwappedOutPages())
        self.assertEqual(region.GetNumSwappedOutPages(), 0)

    def test_missing_info(self):
        """
        Stubs that don't send the new keys (e.g. non-macOS stubs) should
        leave the new accessors reporting "unavailable" rather than
        fabricating values.
        """

        class MyResponder(MockGDBServerResponder):
            def qHostInfo(self):
                return "ptrsize:8;endian:little;vm-page-size:4096;"

            def qMemoryRegionInfo(self, addr):
                return "start:1000;size:4000;permissions:rw;"

        self.server.responder = MyResponder()
        target = self.dbg.CreateTarget("")
        process = self.connect(target)
        lldbutil.expect_state_changes(
            self, self.dbg.GetListener(), process, [lldb.eStateStopped]
        )

        region = lldb.SBMemoryRegionInfo()
        err = process.GetMemoryRegionInfo(0x1000, region)
        self.assertSuccess(err)

        self.assertFalse(region.IsMaxReadable())
        self.assertFalse(region.IsMaxWritable())
        self.assertFalse(region.IsMaxExecutable())
        self.assertFalse(region.HasShareMode())
        self.assertFalse(region.HasRegionTypeTag())
        self.assertFalse(region.IsSubmap())
        self.assertFalse(region.HasNumResidentPages())
        self.assertFalse(region.HasNumDirtiedPages())
        self.assertFalse(region.HasNumSwappedOutPages())
