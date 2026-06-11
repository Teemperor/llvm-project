"""
Test lldb-dap launch request.
"""

from lldbsuite.test.decorators import skipIfNetBSD, skipIf
import lldbdap_testcase


class TestDAP_launch_terminate_commands(lldbdap_testcase.DAPTestCaseBase):
    """
    Tests that the "terminateCommands", that can be passed during
    launch, are run when the debugger is disconnected.
    """

    @skipIfNetBSD  # Hangs on NetBSD as well
    @skipIf(archs=["arm$", "aarch64"], oslist=["linux"])
    def test(self):
        self.build_and_create_debug_adapter()
        program = self.getBuildArtifact("a.out")

        terminateCommands = ["expr 4+2"]
        self.launch(
            program,
            stopOnEntry=True,
            terminateCommands=terminateCommands,
            disconnectAutomatically=False,
        )
        self.get_console()
        # Once it's disconnected the console should contain the
        # "terminateCommands"
        self.dap_server.request_disconnect(terminateDebuggee=True)
        output = self.collect_console(pattern=terminateCommands[0])
        self.verify_commands("terminateCommands", output, terminateCommands)

    @skipIfNetBSD  # Hangs on NetBSD as well
    @skipIf(archs=["arm$", "aarch64"], oslist=["linux"])
    def test_failing_terminate_commands(self):
        """
        Tests that a failing required "terminateCommands" entry makes the
        disconnect response report the error instead of silently succeeding.
        """
        self.build_and_create_debug_adapter()
        program = self.getBuildArtifact("a.out")

        # `!` marks the command as required - failure aborts the command list
        # and propagates an error out of the disconnect handler.
        terminateCommands = ["!settings set this.does.not.exist 1"]
        self.launch(
            program,
            stopOnEntry=True,
            terminateCommands=terminateCommands,
            disconnectAutomatically=False,
        )
        self.get_console()

        response = self.dap_server.request_disconnect(terminateDebuggee=True)
        self.assertFalse(response["success"])
        self.assertRegex(
            response["body"]["error"]["format"],
            r"Failed to run terminateCommands commands\. See the Debug Console for more details",
        )

        output = self.collect_console(pattern=terminateCommands[0])
        self.verify_commands("terminateCommands", output, terminateCommands)
