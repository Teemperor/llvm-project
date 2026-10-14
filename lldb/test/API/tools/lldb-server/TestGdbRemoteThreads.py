"""
Tests for thread-related gdbremote packets (qThreadInfo, Hg, Hc/C).

Tests run against debugserver and lldb-server (llgs).
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
class LldbGdbServerThreadsTestCase(
    gdbremote_testcase.GdbRemoteTestCaseBase, DwarfOpcodeParser
):
    SHARED_BUILD_TESTCASE = False

    def qThreadInfo_contains_thread(self):
        procs = self.prep_debug_monitor_and_inferior()
        self.add_threadinfo_collection_packets()

        # Run the packet stream.
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Gather threadinfo entries.
        threads = self.parse_threadinfo_packets(context)
        self.assertIsNotNone(threads)

        # We should have exactly one thread.
        self.assertEqual(len(threads), 1)

    def test_qThreadInfo_contains_thread_launch(self):
        self.build()
        self.set_inferior_startup_launch()
        self.qThreadInfo_contains_thread()

    @expectedFailureAll(oslist=["windows"])  # expect one more thread stopped
    def test_qThreadInfo_contains_thread_attach(self):
        self.build()
        self.set_inferior_startup_attach()
        self.qThreadInfo_contains_thread()

    def qThreadInfo_matches_qC(self):
        procs = self.prep_debug_monitor_and_inferior()

        self.add_threadinfo_collection_packets()
        self.test_sequence.add_log_lines(
            [
                "read packet: $qC#00",
                {
                    "direction": "send",
                    "regex": r"^\$QC([0-9a-fA-F]+)#",
                    "capture": {1: "thread_id"},
                },
            ],
            True,
        )

        # Run the packet stream.
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Gather threadinfo entries.
        threads = self.parse_threadinfo_packets(context)
        self.assertIsNotNone(threads)

        # We should have exactly one thread from threadinfo.
        self.assertEqual(len(threads), 1)

        # We should have a valid thread_id from $QC.
        QC_thread_id_hex = context.get("thread_id")
        self.assertIsNotNone(QC_thread_id_hex)
        QC_thread_id = int(QC_thread_id_hex, 16)

        # Those two should be the same.
        self.assertEqual(threads[0], QC_thread_id)

    def test_qThreadInfo_matches_qC_launch(self):
        self.build()
        self.set_inferior_startup_launch()
        self.qThreadInfo_matches_qC()

    # This test is flaky on AArch64 Linux. Sometimes it causes an unhandled Error:
    # Operation not permitted in lldb_private::process_linux::NativeProcessLinux::Attach(int).
    @skipIf(
        oslist=["linux"],
        archs=["aarch64"],
        bugnumber="github.com/llvm/llvm-project/issues/138085",
    )
    @expectedFailureAll(oslist=["windows"])  # expect one more thread stopped
    def test_qThreadInfo_matches_qC_attach(self):
        self.build()
        self.set_inferior_startup_attach()
        self.qThreadInfo_matches_qC()

    def Hg_switches_to_3_threads(self, pass_pid=False):
        _, threads = self.launch_with_threads(3)

        pid_str = ""
        if pass_pid:
            pid_str = "p{0:x}.".format(procs["inferior"].pid)

        # verify we can $H to each thead, and $qC matches the thread we set.
        for thread in threads:
            # Change to each thread, verify current thread id.
            self.reset_test_sequence()
            self.test_sequence.add_log_lines(
                [
                    "read packet: $Hg{0}{1:x}#00".format(
                        pid_str, thread
                    ),  # Set current thread.
                    "send packet: $OK#00",
                    "read packet: $qC#00",
                    {
                        "direction": "send",
                        "regex": r"^\$QC([0-9a-fA-F]+)#",
                        "capture": {1: "thread_id"},
                    },
                ],
                True,
            )

            context = self.expect_gdbremote_sequence()
            self.assertIsNotNone(context)

            # Verify the thread id.
            self.assertIsNotNone(context.get("thread_id"))
            self.assertEqual(int(context.get("thread_id"), 16), thread)

    # This test is flaky on Windows. Sometimes returns '$E37#af'.
    @skipIf(compiler="clang", compiler_version=["<", "11.0"])
    def test_Hg_switches_to_3_threads_launch(self):
        self.build()
        self.set_inferior_startup_launch()
        self.Hg_switches_to_3_threads()

    def Hg_fails_on_pid(self, pass_pid):
        _, threads = self.launch_with_threads(2)

        if pass_pid == -1:
            pid_str = "p-1."
        else:
            pid_str = "p{0:x}.".format(pass_pid)
        thread = threads[1]

        self.test_sequence.add_log_lines(
            [
                "read packet: $Hg{0}{1:x}#00".format(
                    pid_str, thread
                ),  # Set current thread.
                "send packet: $Eff#00",
            ],
            True,
        )

        self.expect_gdbremote_sequence()

    @add_test_categories(["llgs"])
    def test_Hg_fails_on_another_pid(self):
        self.build()
        self.set_inferior_startup_launch()
        self.Hg_fails_on_pid(1)

    @add_test_categories(["llgs"])
    def test_Hg_fails_on_zero_pid(self):
        self.build()
        self.set_inferior_startup_launch()
        self.Hg_fails_on_pid(0)

    @add_test_categories(["llgs"])
    def test_Hg_fails_on_minus_one_pid(self):
        self.build()
        self.set_inferior_startup_launch()
        self.Hg_fails_on_pid(-1)

    def Hc_then_Csignal_signals_correct_thread(self, segfault_signo):
        # NOTE only run this one in inferior-launched mode: we can't grab inferior stdout when running attached,
        # and the test requires getting stdout from the exe.

        NUM_THREADS = 3

        # Startup the inferior with three threads (main + NUM_THREADS-1 worker threads).
        # inferior_args=["thread:print-ids"]
        inferior_args = ["thread:segfault"]
        for i in range(NUM_THREADS - 1):
            # if i > 0:
            # Give time between thread creation/segfaulting for the handler to work.
            # inferior_args.append("sleep:1")
            inferior_args.append("thread:new")
        inferior_args.append("sleep:10")

        # Launch/attach.  (In our case, this should only ever be launched since
        # we need inferior stdout/stderr).
        procs = self.prep_debug_monitor_and_inferior(inferior_args=inferior_args)
        self.test_sequence.add_log_lines(["read packet: $c#63"], True)
        context = self.expect_gdbremote_sequence()

        signaled_tids = {}
        print_thread_ids = {}

        # Switch to each thread, deliver a signal, and verify signal delivery
        for i in range(NUM_THREADS - 1):
            # Run until SIGSEGV comes in.
            self.reset_test_sequence()
            self.test_sequence.add_log_lines(
                [
                    {
                        "direction": "send",
                        "regex": r"^\$T([0-9a-fA-F]{2})thread:([0-9a-fA-F]+);",
                        "capture": {1: "signo", 2: "thread_id"},
                    }
                ],
                True,
            )

            context = self.expect_gdbremote_sequence()
            self.assertIsNotNone(context)
            signo = context.get("signo")
            self.assertEqual(int(signo, 16), segfault_signo)

            # Ensure we haven't seen this tid yet.
            thread_id = int(context.get("thread_id"), 16)
            self.assertNotIn(thread_id, signaled_tids)
            signaled_tids[thread_id] = 1

            # Send SIGUSR1 to the thread that signaled the SIGSEGV.
            self.reset_test_sequence()
            self.test_sequence.add_log_lines(
                [
                    # Set the continue thread.
                    # Set current thread.
                    "read packet: $Hc{0:x}#00".format(thread_id),
                    "send packet: $OK#00",
                    # Continue sending the signal number to the continue thread.
                    # The commented out packet is a way to do this same operation without using
                    # a $Hc (but this test is testing $Hc, so we'll stick with the former).
                    "read packet: $C{0:x}#00".format(
                        lldbutil.get_signal_number("SIGUSR1")
                    ),
                    # "read packet: $vCont;C{0:x}:{1:x};c#00".format(lldbutil.get_signal_number('SIGUSR1'), thread_id),
                    # FIXME: Linux does not report the thread stop on the delivered signal (SIGUSR1 here).  MacOSX debugserver does.
                    # But MacOSX debugserver isn't guaranteeing the thread the signal handler runs on, so currently its an XFAIL.
                    # Need to rectify behavior here.  The linux behavior is more intuitive to me since we're essentially swapping out
                    # an about-to-be-delivered signal (for which we already sent a stop packet) to a different signal.
                    # {"direction":"send", "regex":r"^\$T([0-9a-fA-F]{2})thread:([0-9a-fA-F]+);", "capture":{1:"stop_signo", 2:"stop_thread_id"} },
                    #  "read packet: $c#63",
                    {
                        "type": "output_match",
                        "regex": r"^received SIGUSR1 on thread id: ([0-9a-fA-F]+)\r\nthread ([0-9a-fA-F]+): past SIGSEGV\r\n",
                        "capture": {1: "print_thread_id", 2: "post_handle_thread_id"},
                    },
                ],
                True,
            )

            # Run the sequence.
            context = self.expect_gdbremote_sequence()
            self.assertIsNotNone(context)

            # Ensure the stop signal is the signal we delivered.
            # stop_signo = context.get("stop_signo")
            # self.assertIsNotNone(stop_signo)
            # self.assertEqual(int(stop_signo,16), lldbutil.get_signal_number('SIGUSR1'))

            # Ensure the stop thread is the thread to which we delivered the signal.
            # stop_thread_id = context.get("stop_thread_id")
            # self.assertIsNotNone(stop_thread_id)
            # self.assertEqual(int(stop_thread_id,16), thread_id)

            # Ensure we haven't seen this thread id yet.  The inferior's
            # self-obtained thread ids are not guaranteed to match the stub
            # tids (at least on MacOSX).
            print_thread_id = context.get("print_thread_id")
            self.assertIsNotNone(print_thread_id)
            print_thread_id = int(print_thread_id, 16)
            self.assertNotIn(print_thread_id, print_thread_ids)

            # Now remember this print (i.e. inferior-reflected) thread id and
            # ensure we don't hit it again.
            print_thread_ids[print_thread_id] = 1

            # Ensure post signal-handle thread id matches the thread that
            # initially raised the SIGSEGV.
            post_handle_thread_id = context.get("post_handle_thread_id")
            self.assertIsNotNone(post_handle_thread_id)
            post_handle_thread_id = int(post_handle_thread_id, 16)
            self.assertEqual(post_handle_thread_id, print_thread_id)

    @expectedFailureDarwin
    @requireSignals
    @expectedFailureNetBSD
    def test_Hc_then_Csignal_signals_correct_thread_launch(self):
        self.build()
        self.set_inferior_startup_launch()

        if self.platformIsDarwin():
            # Darwin debugserver translates some signals like SIGSEGV into some gdb
            # expectations about fixed signal numbers.
            self.Hc_then_Csignal_signals_correct_thread(self.TARGET_EXC_BAD_ACCESS)
        else:
            self.Hc_then_Csignal_signals_correct_thread(
                lldbutil.get_signal_number("SIGSEGV")
            )
