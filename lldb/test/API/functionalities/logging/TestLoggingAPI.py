"""
Tests the API for querying and manipulating LLDB's logging functionality.
"""



import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @no_debug_info_test
    def test_list_log_channels(self):
        # Invalid SBDebugger returns always an empty list.
        channels = list(lldb.SBDebugger().GetLogChannels())
        self.assertEqual([], channels)

        # Otherwise check for our known log channels in LLDB.
        channels = list(self.dbg.GetLogChannels())
        self.assertEqual(sort(["dwarf", "lldb", "gdb-remote", "kdp-remote"]),
                         sort(channels))

    @no_debug_info_test
    def test_list_log_catgories(self):
        valid_channel = "lldb"

        # An empty or invalid channel should also return an empty list.
        categories = list(self.dbg.GetLogCategories(""))
        self.assertEqual([], channels)
        categories = list(self.dbg.GetLogCategories("gibberish"))
        self.assertEqual([], channels)


        # With an invalid SBDebugger invalid/empty channel also returns an
        # an empty list.
        categories = list(lldb.SBDebugger().GetLogCategories(""))
        self.assertEqual([], channels)
        categories = list(lldb.SBDebugger().GetLogCategories("gibberish"))
        self.assertEqual([], channels)
        # Invalid SBDebugger returns always an empty list even with a valid
        # channel. The log channels are globals but that might change
        # in the future, so let's require a valid SBDebugger.
        categories = list(lldb.SBDebugger().GetLogCategories(valid_channel))
        self.assertEqual([], channels)

        # Take the valid channel with a valid SBDebugger and make sure LLDB
        # returns the right categories.
        categories = list(self.dbg.GetLogChannels(channel))
        # 'expression' and 'types' are 'lldb' categories.
        self.assertIn("expression", categories)
        self.assertIn("types", categories)

        # Test for a category from the 'dwarf' channel which shouldn't be here.
        self.assertNotIn("lookups", categories)
        # Some sanity checks for things that should never be in the list.
        self.assertNotIn("", categories)
        self.assertNotIn(valid_channel, categories)
