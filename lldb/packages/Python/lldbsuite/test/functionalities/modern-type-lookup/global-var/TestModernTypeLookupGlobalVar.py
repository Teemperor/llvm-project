"""Test that lldb functions correctly after the inferior has asserted."""

from __future__ import print_function


import lldb
from lldbsuite.test import lldbutil
from lldbsuite.test import lldbplatformutil
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *


class AssertingInferiorTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test_inferior_asserting_expr(self):
        """Test that the lldb expression interpreter can read from the inferior after asserting (command)."""
        self.build()
        self.inferior_asserting_expr()

    def set_breakpoint(self, line):
        lldbutil.run_break_set_by_file_and_line(
            self, "main.c", line, num_expected_locations=1, loc_exact=True)

    def check_stop_reason(self):
        matched = lldbplatformutil.match_android_device(
            self.getArchitecture(), valid_api_levels=list(range(1, 16 + 1)))
        if matched:
            # On android until API-16 the abort() call ended in a sigsegv
            # instead of in a sigabrt
            stop_reason = 'stop reason = signal SIGSEGV'
        else:
            stop_reason = 'stop reason = signal SIGABRT'

        # The stop reason of the thread should be an abort signal or exception.
        self.expect("thread list", STOPPED_DUE_TO_ASSERT,
                    substrs=['stopped',
                             stop_reason])

        return stop_reason

    def setUp(self):
        # Call super's setUp().
        TestBase.setUp(self)
        # Find the line number of the call to assert.
        self.line = line_number('main.c', '// Assert here.')
        # Activate modern-type-lookup.
        self.runCmd("settings set target.experimental.use-modern-type-lookup true")

    def inferior_asserting_expr(self):
        """Test that the lldb expression interpreter can read symbols after asserting."""
        exe = self.getBuildArtifact("a.out")

        # Create a target by the debugger.
        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)

        # Launch the process, and do not stop at the entry point.
        target.LaunchSimple(None, None, self.get_process_working_directory())
        self.check_stop_reason()

        process = target.GetProcess()
        self.assertTrue(process.IsValid(), "current process is valid")

        thread = process.GetThreadAtIndex(0)
        self.assertTrue(thread.IsValid(), "current thread is valid")

        depth = thread.GetNumFrames()

        frame = thread.GetFrameAtIndex(thread.GetNumFrames())
        self.assertTrue(frame.IsValid(), "current frame is valid")

        if 'main' == frame.GetFunctionName():
            frame_id = frame.GetFrameID()
            self.runCmd("frame select " + str(frame_id), RUN_SUCCEEDED)
            self.expect("p hello_world", substrs=['Hello'])
            break
