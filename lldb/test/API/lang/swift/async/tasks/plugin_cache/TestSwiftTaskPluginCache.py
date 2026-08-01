"""
The Swift Tasks plugin caches the location of each thread's current-task
pointer, and in assert builds it re-verifies that cache every time the thread
list is rebuilt. Walk through async code, stopping many times with several
tasks alive, so that the cached path is taken repeatedly.
"""

import re

import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil

TASK_NAME = re.compile(r"^Task ([0-9]+)$")


class TestCase(lldbtest.TestBase):
    def setUp(self):
        super().setUp()
        self.runCmd("settings set target.experimental.swift-tasks-plugin-enabled true")

    def task_threads(self, process):
        """Map every task thread to the real thread that is running it."""
        mapping = {}
        for thread in process.threads:
            match = TASK_NAME.match(thread.GetName() or "")
            if not match:
                continue
            self.assertNotEqual(thread.GetQueueName(), None)
            mapping[int(match.group(1))] = thread.GetThreadID()
        return mapping

    @skipEmbeddedSwift
    @swiftTest
    @skipIf(oslist=["windows"])
    def test_cache_is_stable_across_stops(self):
        """The task a thread is running must stay consistent across stops."""
        self.build()
        target, process, thread, breakpoint = lldbutil.run_to_source_breakpoint(
            self, "break in task", lldb.SBFileSpec("main.swift")
        )

        # The tasks plugin turns the tasks into threads, so the breakpoint is
        # reported on a task thread.
        self.assertRegex(thread.GetName(), TASK_NAME)

        # Every task ID must keep denoting the same task for as long as it is
        # alive, no matter how often the thread list is rebuilt.
        threads_of_task = {}
        stops = 0
        while process.GetState() == lldb.eStateStopped and stops < 12:
            for task_id, tid in self.task_threads(process).items():
                previous = threads_of_task.setdefault(task_id, tid)
                self.assertEqual(previous, tid, f"task {task_id} changed threads")
            # `language swift task info` computes the current task's address
            # from scratch, i.e. it does not go through the plugin's cache.
            self.expect("language swift task info", substrs=["address = 0x"])
            stops += 1
            process.Continue()

        self.assertGreater(stops, 4)
        self.assertGreater(len(threads_of_task), 1, "expected concurrent tasks")

    @skipEmbeddedSwift
    @swiftTest
    @skipIf(oslist=["windows"])
    def test_cache_is_stable_across_steps(self):
        """Stepping inside a task must not disturb the plugin's cache."""
        self.build()
        target, process, thread, breakpoint = lldbutil.run_to_source_breakpoint(
            self, "break in task", lldb.SBFileSpec("main.swift")
        )
        target.BreakpointDelete(breakpoint.GetID())

        name = thread.GetName()
        self.assertRegex(name, TASK_NAME)
        for _ in range(10):
            thread.StepInto()
            self.assertEqual(process.GetState(), lldb.eStateStopped)
            # Stepping stays within the same task, so the thread keeps its name.
            self.assertEqual(process.GetSelectedThread().GetName(), name)
