"""
Test python scripted process in lldb
"""

import os

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil
from lldbsuite.test import lldbtest


class PlatformProcessCrashInfoTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def setUp(self):
        TestBase.setUp(self)
        self.source = "main.c"

    def tearDown(self):
        TestBase.tearDown(self)

    def test_file_paths_scripted_process(self):
        """Tests that setting a in/out/err file paths for a scripted process
        changes the respective target.* setting."""
        self.build()
        target = self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.assertTrue(target, VALID_TARGET)

        # FIXME: This should use a proper process plugin but right now this
        # hasn't been implemented yet.
        self.expect("process launch -C NoKnownClass -i in_path -o out_path -e err_path", error=True)
        self.expect("settings show target.input-path", substrs=['"in_path"'])
        self.expect("settings show target.output-path", substrs=['"out_path"'])
        self.expect("settings show target.error-path", substrs=['"err_path"'])


    def test_python_plugin_package(self):
        """Test that the lldb python module has a `plugins.scripted_process`
        package."""
        self.expect('script import lldb.plugins',
                    substrs=["ModuleNotFoundError"], matching=False)

        self.expect('script dir(lldb.plugins)',
                    substrs=["scripted_process"])

        self.expect('script import lldb.plugins.scripted_process',
                    substrs=["ModuleNotFoundError"], matching=False)

        self.expect('script dir(lldb.plugins.scripted_process)',
                    substrs=["ScriptedProcess"])

        self.expect('script from lldb.plugins.scripted_process import ScriptedProcess',
                    substrs=["ImportError"], matching=False)

        self.expect('script dir(ScriptedProcess)',
                    substrs=["launch"])

