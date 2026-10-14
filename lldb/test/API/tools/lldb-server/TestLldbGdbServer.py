"""
Basic launch/attach/continue tests for the gdbremote protocol.

Tests run against debugserver and lldb-server (llgs).
lldb-server tests run where the lldb-server exe is
available.
"""

import binascii
import itertools
import struct

import gdbremote_testcase
import lldbgdbserverutils
from lldbsuite.support import seven
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test.lldbdwarf import *
from lldbsuite.test import lldbutil, lldbplatformutil


# On Linux systems with Yama ptrace_scope = 1 there is a race condition when the
# debugee enables tracing. See https://github.com/llvm/llvm-project/issues/161510.
@skipIfLinux
@skipIfMTE  # MTE security transition shims restrict socket operations.
class LldbGdbServerTestCase(
    gdbremote_testcase.GdbRemoteTestCaseBase, DwarfOpcodeParser
):
    SHARED_BUILD_TESTCASE = False

    def test_thread_suffix_supported(self):
        server = self.connect_to_debug_monitor()
        self.assertIsNotNone(server)

        self.do_handshake()
        self.test_sequence.add_log_lines(
            [
                "lldb-server <  26> read packet: $QThreadSuffixSupported#e4",
                "lldb-server <   6> send packet: $OK#9a",
            ],
            True,
        )

        self.expect_gdbremote_sequence()

    def test_list_threads_in_stop_reply_supported(self):
        server = self.connect_to_debug_monitor()
        self.assertIsNotNone(server)

        self.do_handshake()
        self.test_sequence.add_log_lines(
            [
                "lldb-server <  27> read packet: $QListThreadsInStopReply#21",
                "lldb-server <   6> send packet: $OK#9a",
            ],
            True,
        )
        self.expect_gdbremote_sequence()

    def test_c_packet_works(self):
        self.build()
        procs = self.prep_debug_monitor_and_inferior()
        self.test_sequence.add_log_lines(
            ["read packet: $c#63", "send packet: $W00#00"], True
        )

        self.expect_gdbremote_sequence()

    @skipIfWindows  # No pty support to test any inferior output
    def test_inferior_print_exit(self):
        self.build()
        procs = self.prep_debug_monitor_and_inferior(inferior_args=["hello, world"])
        self.test_sequence.add_log_lines(
            [
                "read packet: $vCont;c#a8",
                {
                    "type": "output_match",
                    "regex": self.maybe_strict_output_regex(r"hello, world\r\n"),
                },
                "send packet: $W00#00",
            ],
            True,
        )

        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

    # Sometimes fails:
    # regex '^\$QC([0-9a-fA-F]+)#' failed to match against content '$E45#ae'
    # See https://github.com/llvm/llvm-project/issues/138085.
    def test_first_launch_stop_reply_thread_matches_first_qC(self):
        self.build()
        procs = self.prep_debug_monitor_and_inferior()
        self.test_sequence.add_log_lines(
            [
                "read packet: $qC#00",
                {
                    "direction": "send",
                    "regex": r"^\$QC([0-9a-fA-F]+)#",
                    "capture": {1: "thread_id_QC"},
                },
                "read packet: $?#00",
                {
                    "direction": "send",
                    "regex": r"^\$T[0-9a-fA-F]{2}thread:([0-9a-fA-F]+)",
                    "capture": {1: "thread_id_?"},
                },
            ],
            True,
        )
        context = self.expect_gdbremote_sequence()
        self.assertEqual(context.get("thread_id_QC"), context.get("thread_id_?"))

    def test_attach_commandline_continue_app_exits(self):
        self.build()
        self.set_inferior_startup_attach()
        attached_file = lldbutil.append_to_process_working_directory(self, "attached")
        procs = self.prep_debug_monitor_and_inferior(
            inferior_args=["waitfile:" + attached_file], inferior_sleep_seconds=0
        )
        lldbutil.create_file_on_target(self, attached_file)
        self.test_sequence.add_log_lines(
            ["read packet: $vCont;c#a8", "send packet: $W00#00"], True
        )
        self.expect_gdbremote_sequence()

        # Wait a moment for completed and now-detached inferior process to
        # clear.
        time.sleep(1)

        if not lldb.remote_platform:
            # Process should be dead now. Reap results.
            poll_result = procs["inferior"].poll()
            self.assertIsNotNone(poll_result)

        # Where possible, verify at the system level that the process is not
        # running.
        self.assertFalse(
            lldbgdbserverutils.process_is_running(procs["inferior"].pid, False)
        )
