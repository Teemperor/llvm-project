import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test.lldbpexpect import PExpectTest

class DarwinNSLogOutputTestCase(PExpectTest):

    mydir = TestBase.compute_mydir(__file__)

    def run_before_log(self, launch_args = []):
        self.build()
        # Disable showing of source lines at our breakpoint.
        # This is necessary for the test, because the very
        # text we want to match for output from the running inferior
        # will show up in the source as well.  We don't want the source
        # output to erroneously make a match with our expected output.
        launch_args = ["-o", "settings set stop-line-count-before 0",
                          "-o", "settings set stop-line-count-after 0"] + launch_args
        self.launch(executable=self.getBuildArtifact("a.out"), extra_args=launch_args)

        self.expect("breakpoint set -p 'About to log'", substrs=["Breakpoint 1", "address ="])
        self.expect("run", substrs=["stop reason = breakpoint 1"])


    # PExpect uses many timeouts internally and doesn't play well
    # under ASAN on a loaded machine..
    @skipIfAsan
    @skipIfEditlineSupportMissing
    def test_nslog_output_is_displayed(self):
        """Test that NSLog() output shows up in the command-line debugger."""
        self.run_before_log()
        self.expect("continue", substrs=["This is a message from NSLog",
                                         "exited with status"])
        self.quit()

    # PExpect uses many timeouts internally and doesn't play well
    # under ASAN on a loaded machine..
    @skipIfAsan
    @skipIfEditlineSupportMissing
    # IDE_DISABLED_OS_ACTIVITY_DT_MODE available since 10.12.
    @skipIf(macos_version=["<", "10.12"])
    def test_nslog_output_is_suppressed_with_env_var(self):
        """Test that NSLog() output does not show up with the ignore env var."""
        self.run_before_log(launch_args=["-o",
            "settings set target.env-vars \"IDE_DISABLED_OS_ACTIVITY_DT_MODE=1\""])
        self.expect("continue", substrs=["This is a message from NSLog"], matching=False)
        self.quit()