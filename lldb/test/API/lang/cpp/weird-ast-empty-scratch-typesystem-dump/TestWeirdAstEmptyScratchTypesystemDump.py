"""
Tests that 'target dump typesystem' (and 'target modules dump ast') never
crash LLDB when run against the various "nothing (interesting) has happened
yet, or everything has already been torn down" states of a target's
per-target shared scratch TypeSystemClang/ASTContext:

  * Immediately after 'target create', before any 'run', breakpoint hit, or
    'expression' command has ever touched the target -- the scratch
    TypeSystemClang/ASTContext may not have been lazily created yet, so the
    dump command must handle a null or default-constructed scratch AST
    gracefully instead of dereferencing a null TypeSystemClang* or an
    uninitialized ASTContext's TranslationUnitDecl.

  * After the inferior process has exited normally (target still valid,
    process torn down).

  * After the inferior process has been forcibly 'kill'-ed while stopped at
    a breakpoint (target still valid, process torn down via a different
    path than a normal exit, and any SymbolFile/Module bookkeeping tied to
    that now-dead process must not be touched by the dump commands).

  * After a subsequent 'run' relaunches the same target and it is 'kill'-ed
    a second time, to make sure repeated create/run/kill cycles interleaved
    with dumps don't accumulate any state that eventually crashes the dump
    commands.

None of these should ever produce anything worse than a clean, well-formed
dump (or a clean "invalid target"-style diagnostic) -- a crash here would
mean the dump commands assume the scratch ASTContext singleton is already
lazily initialized, or that the SymbolFile/Module backing the current
target is still alive.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class WeirdAstEmptyScratchTypesystemDumpTestCase(TestBase):
    def test(self):
        self.build()
        exe = self.getBuildArtifact("a.out")

        # Dump the scratch typesystem, and the per-module AST, right after
        # 'target create' -- before any 'run', breakpoint hit, or
        # 'expression' command has ever run against this target. At this
        # point the per-target shared scratch TypeSystemClang/ASTContext
        # may not have been lazily created yet at all.
        self.runCmd("target create %s" % exe)
        self.expect("target dump typesystem")
        self.expect("target modules dump ast")
        self.expect("target modules dump ast --filter Simple")

        target = self.dbg.GetSelectedTarget()
        self.assertTrue(target and target.IsValid(), "Target should be valid")

        breakpoint = target.BreakpointCreateBySourceRegex(
            "return s.getValue", lldb.SBFileSpec("main.cpp", False)
        )
        self.assertTrue(breakpoint.GetNumLocations() > 0, VALID_BREAKPOINT)

        # Dump again now that a breakpoint exists but the process has not
        # been launched yet.
        self.expect("target dump typesystem")

        self.runCmd("run")
        process = self.process()
        self.assertState(process.GetState(), lldb.eStateStopped)
        thread = lldbutil.get_one_thread_stopped_at_breakpoint(process, breakpoint)
        self.assertIsNotNone(
            thread, "Process should be stopped at the breakpoint in main"
        )

        # Now that we are actually stopped, dumping should still behave
        # normally (this also lazily creates the scratch AST if the prior
        # dumps did not already).
        self.expect("target dump typesystem")
        self.expect("target modules dump ast --filter Simple")

        # Forcibly kill the inferior (rather than letting it run to
        # completion) while it is stopped at a breakpoint, then dump the
        # scratch typesystem and per-module AST again. The target object is
        # still valid, but the process backing it has been torn down via
        # 'kill' rather than a normal exit.
        self.runCmd("kill")
        self.expect("target dump typesystem")
        self.expect("target modules dump ast --filter Simple")

        # Relaunch the very same target, hit the same breakpoint again, and
        # kill it a second time, dumping in between each step. This makes
        # sure repeated create/run/kill cycles interleaved with dumps don't
        # eventually corrupt the scratch AST or leave a stale
        # SymbolFile/Module pointer that a later dump would dereference.
        self.runCmd("run")
        process = self.process()
        self.assertState(process.GetState(), lldb.eStateStopped)
        thread = lldbutil.get_one_thread_stopped_at_breakpoint(process, breakpoint)
        self.assertIsNotNone(
            thread, "Process should be stopped at the breakpoint in main"
        )

        self.expect("target dump typesystem")

        self.runCmd("kill")
        self.expect("target dump typesystem", substrs=["Simple"])
        self.expect("target modules dump ast --filter Simple")

        # Finally, let a fresh run of the process exit normally (rather
        # than being killed), and dump one more time: the target is still
        # valid, but the process has now exited on its own.
        self.runCmd("run")
        process = self.process()
        self.assertState(process.GetState(), lldb.eStateStopped)
        self.runCmd("continue")
        self.assertState(process.GetState(), lldb.eStateExited)

        self.expect("target dump typesystem", substrs=["Simple"])
        self.expect("target modules dump ast --filter Simple")
