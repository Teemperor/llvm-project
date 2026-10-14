"""
Tests for register read/write gdbremote packets (p, P).

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
class LldbGdbServerRegistersTestCase(
    gdbremote_testcase.GdbRemoteTestCaseBase, DwarfOpcodeParser
):
    SHARED_BUILD_TESTCASE = False

    def test_p_returns_correct_data_size_for_each_qRegisterInfo_launch(self):
        self.build()
        self.set_inferior_startup_launch()
        procs = self.prep_debug_monitor_and_inferior()
        self.add_register_info_collection_packets()

        # Run the packet stream.
        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Gather register info entries.
        reg_infos = self.parse_register_info_packets(context)
        self.assertIsNotNone(reg_infos)
        self.assertGreater(len(reg_infos), 0)

        byte_order = self.get_target_byte_order()

        # Read value for each register.
        reg_index = 0
        for reg_info in reg_infos:
            # Skip registers that don't have a register set.  For x86, these are
            # the DRx registers, which have no LLDB-kind register number and thus
            # cannot be read via normal
            # NativeRegisterContext::ReadRegister(reg_info,...) calls.
            if not "set" in reg_info:
                continue

            # Clear existing packet expectations.
            self.reset_test_sequence()

            # Run the register query
            self.test_sequence.add_log_lines(
                [
                    "read packet: $p{0:x}#00".format(reg_index),
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

            # Verify the response length.
            p_response = context.get("p_response")
            self.assertIsNotNone(p_response)

            # Skip erraneous (unsupported) registers.
            # TODO: remove this once we make unsupported registers disappear.
            if p_response.startswith("E") and len(p_response) == 3:
                continue

            if "dynamic_size_dwarf_expr_bytes" in reg_info:
                self.updateRegInfoBitsize(reg_info, byte_order)
            self.assertEqual(
                len(p_response), 2 * int(reg_info["bitsize"]) / 8, reg_info
            )

            # Increment loop
            reg_index += 1

    # Note: as of this moment, a hefty number of the GPR writes are failing with E32 (everything except rax-rdx, rdi, rsi, rbp).
    # Come back to this.  I have the test rigged to verify that at least some
    # of the bit-flip writes work.
    def test_P_writes_all_gpr_registers(self):
        self.build()
        self.set_inferior_startup_launch()

        # Start inferior debug session, grab all register info.
        procs = self.prep_debug_monitor_and_inferior(inferior_args=["sleep:2"])
        self.add_register_info_collection_packets()
        self.add_process_info_collection_packets()

        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        # Process register infos.
        reg_infos = self.parse_register_info_packets(context)
        self.assertIsNotNone(reg_infos)
        self.add_lldb_register_index(reg_infos)

        # Process endian.
        process_info = self.parse_process_info_response(context)
        endian = process_info.get("endian")
        self.assertIsNotNone(endian)

        # Pull out the register infos that we think we can bit flip
        # successfully,.
        gpr_reg_infos = [
            reg_info
            for reg_info in reg_infos
            if self.is_bit_flippable_register(reg_info)
        ]
        self.assertGreater(len(gpr_reg_infos), 0)

        # Write flipped bit pattern of existing value to each register.
        successful_writes, failed_writes = self.flip_all_bits_in_each_register_value(
            gpr_reg_infos, endian
        )
        self.trace(
            "successful writes: {}, failed writes: {}".format(
                successful_writes, failed_writes
            )
        )
        self.assertGreater(successful_writes, 0)

    # Note: as of this moment, a hefty number of the GPR writes are failing
    # with E32 (everything except rax-rdx, rdi, rsi, rbp).
    @skipIfWindows
    def test_P_and_p_thread_suffix_work(self):
        self.build()
        self.set_inferior_startup_launch()

        # Startup the inferior with three threads.
        _, threads = self.launch_with_threads(3)

        self.reset_test_sequence()
        self.add_thread_suffix_request_packets()
        self.add_register_info_collection_packets()
        self.add_process_info_collection_packets()

        context = self.expect_gdbremote_sequence()
        self.assertIsNotNone(context)

        process_info = self.parse_process_info_response(context)
        self.assertIsNotNone(process_info)
        endian = process_info.get("endian")
        self.assertIsNotNone(endian)

        reg_infos = self.parse_register_info_packets(context)
        self.assertIsNotNone(reg_infos)
        self.add_lldb_register_index(reg_infos)

        reg_index = self.select_modifiable_register(reg_infos)
        self.assertIsNotNone(reg_index)
        reg_byte_size = int(reg_infos[reg_index]["bitsize"]) // 8
        self.assertGreater(reg_byte_size, 0)

        expected_reg_values = []
        register_increment = 1
        next_value = None

        # Set the same register in each of 3 threads to a different value.
        # Verify each one has the unique value.
        for thread in threads:
            # If we don't have a next value yet, start it with the initial read
            # value + 1
            if not next_value:
                # Read pre-existing register value.
                self.reset_test_sequence()
                self.test_sequence.add_log_lines(
                    [
                        "read packet: $p{0:x};thread:{1:x}#00".format(
                            reg_index, thread
                        ),
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

                # Set the next value to use for writing as the increment plus
                # current value.
                p_response = context.get("p_response")
                self.assertIsNotNone(p_response)
                next_value = lldbgdbserverutils.unpack_register_hex_unsigned(
                    endian, p_response
                )

            # Set new value using P and thread suffix.
            self.reset_test_sequence()
            self.test_sequence.add_log_lines(
                [
                    "read packet: $P{0:x}={1};thread:{2:x}#00".format(
                        reg_index,
                        lldbgdbserverutils.pack_register_hex(
                            endian, next_value, byte_size=reg_byte_size
                        ),
                        thread,
                    ),
                    "send packet: $OK#00",
                ],
                True,
            )
            context = self.expect_gdbremote_sequence()
            self.assertIsNotNone(context)

            # Save the value we set.
            expected_reg_values.append(next_value)

            # Increment value for next thread to use (we want them all
            # different so we can verify they wrote to each thread correctly
            # next.)
            next_value += register_increment

        # Revisit each thread and verify they have the expected value set for
        # the register we wrote.
        thread_index = 0
        for thread in threads:
            # Read pre-existing register value.
            self.reset_test_sequence()
            self.test_sequence.add_log_lines(
                [
                    "read packet: $p{0:x};thread:{1:x}#00".format(reg_index, thread),
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

            # Get the register value.
            p_response = context.get("p_response")
            self.assertIsNotNone(p_response)
            read_value = lldbgdbserverutils.unpack_register_hex_unsigned(
                endian, p_response
            )

            # Make sure we read back what we wrote.
            self.assertEqual(read_value, expected_reg_values[thread_index])
            thread_index += 1
