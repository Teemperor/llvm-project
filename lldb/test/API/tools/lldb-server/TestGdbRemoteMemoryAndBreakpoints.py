"""
Tests for memory (m, M), breakpoint (Z/z) and siginfo gdbremote packets.

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
class LldbGdbServerMemoryTestCase(
    gdbremote_testcase.GdbRemoteTestCaseBase, DwarfOpcodeParser
):
    SHARED_BUILD_TESTCASE = False

    @skipIfWindows  # No pty support to test any inferior output
    def test_m_packet_reads_memory(self):
        self.build()
        self.set_inferior_startup_launch()
        # This is the memory we will write into the inferior and then ensure we
        # can read back with $m.
        MEMORY_CONTENTS = "Test contents 0123456789 ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz"

        # Start up the inferior.
        procs = self.prep_debug_monitor_and_inferior(
            inferior_args=[
                "set-message:%s" % MEMORY_CONTENTS,
                "get-data-address-hex:g_message",
                "sleep:5",
            ]
        )

        # Run the process
        self.test_sequence.add_log_lines(
            [
                # Start running after initial stop.
                "read packet: $c#63",
                # Match output line that prints the memory address of the message buffer within the inferior.
                # Note we require launch-only testing so we can get inferior otuput.
                {
                    "type": "output_match",
                    "regex": self.maybe_strict_output_regex(
                        r"data address: 0x([0-9a-fA-F]+)\r\n"
                    ),
                    "capture": {1: "message_address"},
                },
                # Now stop the inferior.
                "read packet: {}".format(chr(3)),
                # And wait for the stop notification.
                {
                    "direction": "send",
                    "regex": r"^\$T([0-9a-fA-F]{2})thread:([0-9a-fA-F]+);",
                    "capture": {1: "stop_signo", 2: "stop_thread_id"},
                },
            ],
            True,
        )

        # Run the packet stream.
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Grab the message address.
        self.assertIsNotNone(context.get("message_address"))
        message_address = int(context.get("message_address"), 16)

        # Grab contents from the inferior.
        self.reset_test_sequence()
        self.test_sequence.add_log_lines(
            [
                "read packet: $m{0:x},{1:x}#00".format(
                    message_address, len(MEMORY_CONTENTS)
                ),
                {
                    "direction": "send",
                    "regex": r"^\$(.+)#[0-9a-fA-F]{2}$",
                    "capture": {1: "read_contents"},
                },
            ],
            True,
        )

        # Run the packet stream.
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Ensure what we read from inferior memory is what we wrote.
        self.assertIsNotNone(context.get("read_contents"))
        read_contents = seven.unhexlify(context.get("read_contents"))
        self.assertEqual(read_contents, MEMORY_CONTENTS)

    def breakpoint_set_and_remove_work(self, want_hardware):
        # Start up the inferior.
        procs = self.prep_debug_monitor_and_inferior(
            inferior_args=[
                "get-code-address-hex:hello",
                "sleep:1",
                "call-function:hello",
            ]
        )

        # Run the process
        self.add_register_info_collection_packets()
        self.add_process_info_collection_packets()
        self.test_sequence.add_log_lines(
            [  # Start running after initial stop.
                "read packet: $c#63",
                # Match output line that prints the memory address of the function call entry point.
                # Note we require launch-only testing so we can get inferior otuput.
                {
                    "type": "output_match",
                    "regex": self.maybe_strict_output_regex(
                        r"code address: 0x([0-9a-fA-F]+)\r\n"
                    ),
                    "capture": {1: "function_address"},
                },
                # Now stop the inferior.
                "read packet: {}".format(chr(3)),
                # And wait for the stop notification.
                {
                    "direction": "send",
                    "regex": r"^\$T([0-9a-fA-F]{2})thread:([0-9a-fA-F]+);",
                    "capture": {1: "stop_signo", 2: "stop_thread_id"},
                },
            ],
            True,
        )

        # Run the packet stream.
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Gather process info - we need endian of target to handle register
        # value conversions.
        process_info = self.parse_process_info_response(context)
        endian = process_info.get("endian")
        self.assertIsNotNone(endian)

        # Gather register info entries.
        reg_infos = self.parse_register_info_packets(context)
        pc_lldb_reg_index, pc_reg_info = self.find_pc_reg_info(reg_infos)
        self.assertIsNotNone(pc_lldb_reg_index)
        self.assertIsNotNone(pc_reg_info)

        # Grab the function address.
        self.assertIsNotNone(context.get("function_address"))
        function_address = int(context.get("function_address"), 16)

        # Get current target architecture
        target_arch = self.getArchitecture()

        # Set the breakpoint.
        if target_arch in ["arm", "arm64", "aarch64", "arm64e"]:
            # TODO: Handle case when setting breakpoint in thumb code
            BREAKPOINT_KIND = 4
        else:
            BREAKPOINT_KIND = 1

        # Set default packet type to Z0 (software breakpoint)
        z_packet_type = 0

        # If hardware breakpoint is requested set packet type to Z1
        if want_hardware:
            z_packet_type = 1

        self.reset_test_sequence()
        self.add_set_breakpoint_packets(
            function_address,
            z_packet_type,
            do_continue=True,
            breakpoint_kind=BREAKPOINT_KIND,
        )

        # Run the packet stream.
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Verify the stop signal reported was the breakpoint signal number.
        stop_signo = context.get("stop_signo")
        self.assertIsNotNone(stop_signo)
        self.assertEqual(int(stop_signo, 16), lldbutil.get_signal_number("SIGTRAP"))

        # Ensure we did not receive any output.  If the breakpoint was not set, we would
        # see output (from a launched process with captured stdio) printing a hello, world message.
        # That would indicate the breakpoint didn't take.
        self.assertEqual(len(context["O_content"]), 0)

        # Verify that the PC for the main thread is where we expect it - right at the breakpoint address.
        # This acts as a another validation on the register reading code.
        self.reset_test_sequence()
        self.test_sequence.add_log_lines(
            [
                # Print the PC.  This should match the breakpoint address.
                "read packet: $p{0:x}#00".format(pc_lldb_reg_index),
                # Capture $p results.
                {
                    "direction": "send",
                    "regex": r"^\$([0-9a-fA-F]+)#",
                    "capture": {1: "p_response"},
                },
            ],
            True,
        )

        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Verify the PC is where we expect.  Note response is in endianness of
        # the inferior.
        p_response = context.get("p_response")
        self.assertIsNotNone(p_response)

        # Convert from target endian to int.
        returned_pc = lldbgdbserverutils.unpack_register_hex_unsigned(
            endian, p_response
        )
        self.assertEqual(returned_pc, function_address)

        # Verify that a breakpoint remove and continue gets us the expected
        # output.
        self.reset_test_sequence()

        # Add breakpoint remove packets
        self.add_remove_breakpoint_packets(
            function_address, z_packet_type, breakpoint_kind=BREAKPOINT_KIND
        )

        self.test_sequence.add_log_lines(
            [
                # Continue running.
                "read packet: $c#63",
                # We should now receive the output from the call.
                {"type": "output_match", "regex": r"^hello, world\r\n$"},
                # And wait for program completion.
                {"direction": "send", "regex": r"^\$W00(.*)#[0-9a-fA-F]{2}$"},
            ],
            True,
        )

        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

    @skipIfWindows  # No pty support to test any inferior output
    def test_software_breakpoint_set_and_remove_work(self):
        if self.getArchitecture() == "arm":
            # TODO: Handle case when setting breakpoint in thumb code
            self.build(dictionary={"CFLAGS_EXTRAS": "-marm"})
        else:
            self.build()
        self.set_inferior_startup_launch()
        self.breakpoint_set_and_remove_work(want_hardware=False)

    @skipUnlessPlatform(oslist=["linux"])
    @skipIf(archs=no_match(["arm$", "aarch64"]))
    def test_hardware_breakpoint_set_and_remove_work(self):
        if self.getArchitecture() == "arm":
            # TODO: Handle case when setting breakpoint in thumb code
            self.build(dictionary={"CFLAGS_EXTRAS": "-marm"})
        else:
            self.build()
        self.set_inferior_startup_launch()
        self.breakpoint_set_and_remove_work(want_hardware=True)

    @skipIfWindows  # No pty support to test any inferior output
    def test_written_M_content_reads_back_correctly(self):
        self.build()
        self.set_inferior_startup_launch()

        TEST_MESSAGE = "Hello, memory"

        # Start up the stub and start/prep the inferior.
        procs = self.prep_debug_monitor_and_inferior(
            inferior_args=[
                "set-message:xxxxxxxxxxxxxX",
                "get-data-address-hex:g_message",
                "sleep:1",
                "print-message:",
            ]
        )
        self.test_sequence.add_log_lines(
            [
                # Start running after initial stop.
                "read packet: $c#63",
                # Match output line that prints the memory address of the message buffer within the inferior.
                # Note we require launch-only testing so we can get inferior otuput.
                {
                    "type": "output_match",
                    "regex": self.maybe_strict_output_regex(
                        r"data address: 0x([0-9a-fA-F]+)\r\n"
                    ),
                    "capture": {1: "message_address"},
                },
                # Now stop the inferior.
                "read packet: {}".format(chr(3)),
                # And wait for the stop notification.
                {
                    "direction": "send",
                    "regex": r"^\$T([0-9a-fA-F]{2})thread:([0-9a-fA-F]+);",
                    "capture": {1: "stop_signo", 2: "stop_thread_id"},
                },
            ],
            True,
        )
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Grab the message address.
        self.assertIsNotNone(context.get("message_address"))
        message_address = int(context.get("message_address"), 16)

        # Hex-encode the test message, adding null termination.
        hex_encoded_message = seven.hexlify(TEST_MESSAGE)

        # Write the message to the inferior. Verify that we can read it with the hex-encoded (m)
        # and binary (x) memory read packets.
        self.reset_test_sequence()
        self.test_sequence.add_log_lines(
            [
                "read packet: $M{0:x},{1:x}:{2}#00".format(
                    message_address, len(TEST_MESSAGE), hex_encoded_message
                ),
                "send packet: $OK#00",
                "read packet: $m{0:x},{1:x}#00".format(
                    message_address, len(TEST_MESSAGE)
                ),
                "send packet: ${0}#00".format(hex_encoded_message),
                "read packet: $x{0:x},{1:x}#00".format(
                    message_address, len(TEST_MESSAGE)
                ),
                "send packet: ${0}#00".format(TEST_MESSAGE),
                "read packet: $m{0:x},4#00".format(message_address),
                "send packet: ${0}#00".format(hex_encoded_message[0:8]),
                "read packet: $x{0:x},4#00".format(message_address),
                "send packet: ${0}#00".format(TEST_MESSAGE[0:4]),
                "read packet: $c#63",
                {
                    "type": "output_match",
                    "regex": r"^message: (.+)\r\n$",
                    "capture": {1: "printed_message"},
                },
                "send packet: $W00#00",
            ],
            True,
        )
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Ensure what we read from inferior memory is what we wrote.
        printed_message = context.get("printed_message")
        self.assertIsNotNone(printed_message)
        self.assertEqual(printed_message, TEST_MESSAGE + "X")

    @skipUnlessPlatform(oslist=["freebsd", "linux"])
    @add_test_categories(["llgs"])
    def test_qXfer_siginfo_read(self):
        self.build()
        self.set_inferior_startup_launch()
        procs = self.prep_debug_monitor_and_inferior(
            inferior_args=["thread:segfault", "thread:new", "sleep:10"]
        )
        self.test_sequence.add_log_lines(["read packet: $c#63"], True)
        self.expect_gdbremote_sequence()

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

        # Figure out which thread crashed.
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)
        self.assertEqual(
            int(context["signo"], 16), lldbutil.get_signal_number("SIGSEGV")
        )
        crashing_thread = int(context["thread_id"], 16)

        # Grab siginfo for the crashing thread.
        self.reset_test_sequence()
        self.add_process_info_collection_packets()
        self.test_sequence.add_log_lines(
            [
                "read packet: $Hg{:x}#00".format(crashing_thread),
                "send packet: $OK#00",
                "read packet: $qXfer:siginfo:read::0,80:#00",
                {
                    "direction": "send",
                    "regex": re.compile(
                        r"^\$([^E])(.*)#[0-9a-fA-F]{2}$", re.MULTILINE | re.DOTALL
                    ),
                    "capture": {1: "response_type", 2: "content_raw"},
                },
            ],
            True,
        )
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Ensure we end up with all data in one packet.
        self.assertEqual(context.get("response_type"), "l")

        # Decode binary data.
        content_raw = context.get("content_raw")
        self.assertIsNotNone(content_raw)
        content = self.decode_gdbremote_binary(content_raw).encode("latin1")

        # Decode siginfo_t.
        process_info = self.parse_process_info_response(context)
        pad = ""
        if process_info["ptrsize"] == "8":
            pad = "i"
        signo_idx = 0
        errno_idx = 1
        code_idx = 2
        addr_idx = -1
        SEGV_MAPERR = 1
        if process_info["ostype"] == "linux":
            # si_signo, si_errno, si_code, [pad], _sifields._sigfault.si_addr
            format_str = "iii{}P".format(pad)
        elif process_info["ostype"].startswith("freebsd"):
            # si_signo, si_errno, si_code, si_pid, si_uid, si_status, si_addr
            format_str = "iiiiiiP"
        elif process_info["ostype"].startswith("netbsd"):
            # _signo, _code, _errno, [pad], _reason._fault._addr
            format_str = "iii{}P".format(pad)
            errno_idx = 2
            code_idx = 1
        else:
            assert False, "unknown ostype"

        decoder = struct.Struct(format_str)
        decoded = decoder.unpack(content[: decoder.size])
        self.assertEqual(decoded[signo_idx], lldbutil.get_signal_number("SIGSEGV"))
        self.assertEqual(decoded[errno_idx], 0)  # si_errno
        self.assertEqual(decoded[code_idx], SEGV_MAPERR)  # si_code
        self.assertEqual(decoded[addr_idx], 0)  # si_addr
