"""
Test lldb-dap launch request.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
import lldbdap_testcase
import os
import re


class TestDAP_launch_failing_launch_commands(lldbdap_testcase.DAPTestCaseBase):
    """
    Tests "launchCommands" failures prevents a launch.
    """

    def test(self):
        self.build_and_create_debug_adapter()
        program = self.getBuildArtifact("a.out")

        # Run an invalid launch command, in this case a bad path.
        bad_path = os.path.join("bad", "path")
        launchCommands = ['!target create "%s%s"' % (bad_path, program)]

        initCommands = ["target list", "platform list"]
        preRunCommands = ["image list a.out", "image dump sections a.out"]
        response = self.launch_and_configurationDone(
            program,
            initCommands=initCommands,
            preRunCommands=preRunCommands,
            launchCommands=launchCommands,
        )

        self.assertFalse(response["success"])
        self.assertRegex(
            response["body"]["error"]["format"],
            r"Failed to run launch commands\. See the Debug Console for more details",
        )

        # Get output from the console. This should contain both the
        # "initCommands" and the "preRunCommands".
        output = self.get_console()
        # Verify all "initCommands" were found in console output
        self.verify_commands("initCommands", output, initCommands)
        # Verify all "preRunCommands" were found in console output
        self.verify_commands("preRunCommands", output, preRunCommands)

        # Verify all "launchCommands" were founc in console output
        # The launch should fail due to the invalid command.
        self.verify_commands("launchCommands", output, launchCommands)
        self.assertRegex(output, re.escape(bad_path) + r".*does not exist")

    def _read_dap_log(self):
        """Return the contents of the lldb-dap log file for this test."""
        with open(self.log_files[-1]) as f:
            return f.read()

    def test_failing_post_run_commands(self):
        """
        Tests that a failing required "postRunCommands" entry makes the launch
        response report the error instead of silently succeeding.
        """
        self.build_and_create_debug_adapter()
        program = self.getBuildArtifact("a.out")

        # `!` marks the command as required - failure aborts the command list.
        postRunCommands = ["!settings set this.does.not.exist 1"]
        response = self.launch_and_configurationDone(
            program,
            postRunCommands=postRunCommands,
        )

        self.assertFalse(response["success"])
        self.assertRegex(
            response["body"]["error"]["format"],
            r"Failed to run postRunCommands commands\. See the Debug Console for more details",
        )

        output = self.get_console()
        self.verify_commands("postRunCommands", output, postRunCommands)

    def test_failing_stop_commands(self):
        """
        Tests that a failing required "stopCommands" entry is reported in the
        lldb-dap log. The error is logged-only because the stopped event is
        already sent before stopCommands run, so there is no DAP response to
        attach the error to.
        """
        self.build_and_create_debug_adapter()
        program = self.getBuildArtifact("a.out")

        stopCommands = ["!settings set this.does.not.exist 1"]
        self.launch_and_configurationDone(
            program,
            stopOnEntry=True,
            stopCommands=stopCommands,
        )

        # Hit the stop-on-entry stop, then continue to exit. The `continue`
        # request causes the next event-loop iteration, which guarantees the
        # stop-event handler (and therefore the DAP_LOG_ERROR) has run.
        self.dap_server.wait_for_stopped()
        self.continue_to_exit()

        self.assertIn(
            "Failed to run stopCommands commands. See the Debug Console for more details",
            self._read_dap_log(),
        )

    def test_failing_exit_commands(self):
        """
        Tests that a failing required "exitCommands" entry is reported in the
        lldb-dap log. The error is logged-only because exitCommands run during
        the process-exit event, after the launch response was already sent.
        """
        self.build_and_create_debug_adapter()
        program = self.getBuildArtifact("a.out")

        exitCommands = ["!settings set this.does.not.exist 1"]
        self.launch_and_configurationDone(
            program,
            exitCommands=exitCommands,
        )
        self.continue_to_exit()

        self.assertIn(
            "Failed to run exitCommands commands. See the Debug Console for more details",
            self._read_dap_log(),
        )
