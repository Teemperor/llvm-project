import os
import shutil

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test.lldbpexpect import PExpectTest

class TestCase(PExpectTest):

    mydir = TestBase.compute_mydir(__file__)

    # PExpect uses many timeouts internally and doesn't play well
    # under ASAN on a loaded machine..
    @skipIfAsan
    def test_invalid_paths(self):
        # Invalid path.
        self.launch(extra_args=["-o", "settings set history-directory ' '"])
        self.child.expect_exact("Could not load history file")
        self.quit()

        # Empty path.
        self.launch(extra_args=["-o", "settings set history-directory ''"])
        self.child.expect_exact("Could not load history file")
        self.quit()

        # Inaccessible directory.
        self.launch(extra_args=["-o",
            "settings set history-directory '/lldb_inaccessible_test_dir'"])
        self.child.expect_exact("Could not load history file")
        self.quit()


    def assert_directory_available(self, directory):
        # Create a fresh subdirectory for relative paths.
        fresh_subdir = self.getBuildArtifact("fresh_subdir")
        if os.path.isdir(fresh_subdir):
          shutil.rmtree(fresh_subdir)
        os.mkdir(fresh_subdir)
        os.chdir(fresh_subdir)

        self.launch(extra_args=["-o", "settings set history-directory '" + directory + "'"])
        self.expect("help", substrs=["help"])
        self.quit()

        scanned_paths = []
        found_any_history_file = False
        for entry in os.scandir(directory):
            scanned_paths.append(entry.path)
            if entry.path.endswith("history") and entry.is_file():
                found_any_history_file = True
        self.assertTrue(found_any_history_file, str(scanned_paths))

    # PExpect uses many timeouts internally and doesn't play well
    # under ASAN on a loaded machine..
    @skipIfAsan
    def test_valid_paths(self):
        # Current directory.
        self.assert_directory_available(".")

        # Absolute path to current directory.
        #self.assert_directory_available(self.getBuildDir())

        # Relative path.
        #self.assert_directory_available("lldb")
