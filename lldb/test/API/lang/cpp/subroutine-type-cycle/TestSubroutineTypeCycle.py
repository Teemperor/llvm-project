import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class SubroutineTypeCycleTestCase(TestBase):
    NO_DEBUG_INFO_TESTCASE = True

    @skipIf(compiler="gcc")
    @add_test_categories(["typesystem-clike"])
    @skipIf(
        clike_typesystem=True,
        bugnumber="Checks a DWARFASTParserClang diagnostic for a function type "
        "whose parameter type is still being resolved further up the stack. "
        "DWARFASTParserClike resolves this DWARF without hitting that cycle, "
        "so it has nothing to report.",
    )
    def test(self):
        """A function type whose parameter type is already being resolved
        further up the stack cannot be built. Check that this is reported
        instead of crashing."""
        self.build()
        broadcaster = self.dbg.GetBroadcaster()
        listener = lldbutil.start_listening_from(
            broadcaster, lldb.SBDebugger.eBroadcastBitError
        )
        self.createTestTarget()

        self.expect(
            "image lookup -v -n erase_lambda",
            patterns=['Function: id = .*, name = ".*erase<lambda>'],
        )

        event = lldbutil.fetch_next_event(self, listener, broadcaster)
        diagnostic = lldb.SBDebugger.GetDiagnosticFromEvent(event)
        self.assertIn(
            "DW_TAG_subprogram 'erase<lambda>' has a return or parameter type "
            "that cannot be resolved",
            diagnostic.GetValueForKey("message").GetStringValue(1024),
        )
