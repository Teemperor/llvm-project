"""
Test sending SIGINT to the embedded Python REPL.
"""

import os

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test.lldbpexpect import PExpectTest

class TestCase(PExpectTest):

    mydir = TestBase.compute_mydir(__file__)

    # PExpect uses many timeouts internally and doesn't play well
    # under ASAN on a loaded machine..
    @skipIfAsan
    def test(self):
        self.launch()

        # Start the embedded Python REPL via the 'script' command.
        self.child.send("script -l python --\n")
        # Wait for the Python REPL prompt.
        self.child.expect(">>>")
        # Send SIGINT to the LLDB process.
        self.child.sendintr()
        # This should get transformed to a KeyboardInterrupt which is the same
        # behaviour as the standalone Python REPL.
        self.child.expect("KeyboardInterrupt")
        # Send EOF to quit the Python REPL.
        self.child.sendeof()

        self.quit()
