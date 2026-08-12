"""
Test LLDB's Clang AST machinery (ASTImporter / TypeSystemClang /
DWARFASTParserClang, plus the per-target shared scratch ASTContext) under
concurrent access while a synchronous JIT'd function call is in flight.

The scenario:

  - A dylib defines 'int SlowCompute()', which just sleeps for a few
    seconds when called, and a 'Widget' struct.
  - The main thread issues 'expression -- SlowCompute()'. Evaluating this
    calls SlowCompute() via LLDB's IR interpreter/JIT, which resumes the
    inferior and blocks the calling thread (and LLDB's private state
    thread) until the call returns -- i.e. for the whole multi-second
    sleep.
  - While that call is still in flight, a second Python thread -- driving
    the very same SBDebugger via SBCommandInterpreter.HandleCommand, much
    like a second, independent command-line client would -- repeatedly
    runs 'target dump typesystem' (dumping the per-target shared scratch
    TypeSystem/ASTContext that expression evaluation and the ASTImporter
    import into) and 'target modules dump ast --filter Widget' (dumping
    just the parsed-from-DWARF per-module AST for 'Widget').

This deliberately dumps LLDB's internal Clang AST state while the
private state thread is mid-way through materializing/running a JIT'd
call -- i.e. while the same ASTContext(s) the dump machinery walks may be
getting concurrently read (or, in a buggier world, mutated) by the call
setup/teardown. At a minimum this must never crash LLDB; both the
dumping commands and the blocking expression should eventually complete.
"""

import threading

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTypesystemDumpDuringJitCallTestCase(TestBase):
    def test_dump_typesystem_during_blocking_jit_call(self):
        """
        Forces the dylib's 'Widget' type to be parsed and completed into
        LLDB's Clang AST machinery, then races repeated 'target dump
        typesystem' / 'target modules dump ast --filter Widget' commands
        (issued from a second Python thread via
        SBCommandInterpreter.HandleCommand on the same SBDebugger) against
        a blocking 'expression -- SlowCompute()' JIT call running on the
        main thread. This must not crash LLDB, and both the dump commands
        and the blocking expression must eventually complete.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Dereferencing (rather than just null-checking the pointer)
        # forces LLDB to actually parse and complete the dylib's 'Widget'
        # type -- via DWARFASTParserClang/TypeSystemClang -- into the
        # per-module AST and, from there, the per-target scratch
        # ASTContext.
        self.expect_expr("*plugin_make_widget()", result_type="Widget")

        interp = self.dbg.GetCommandInterpreter()

        # Results collected by the background thread: a list of
        # (command, succeeded, output-length) tuples, one per issued
        # command. Read only after the worker thread has been joined.
        worker_results = []
        worker_exception = []

        def dump_worker():
            try:
                dump_commands = [
                    "target dump typesystem",
                    "target modules dump ast --filter Widget",
                ]
                # Keep hammering both dump commands for as long as the
                # blocking SlowCompute() JIT call on the main thread is
                # likely to still be running. If LLDB is working
                # correctly, every one of these HandleCommand calls
                # should simply succeed (perhaps after being serialized
                # behind the main thread's in-flight expression
                # evaluation) -- never crash the process.
                for i in range(40):
                    command = dump_commands[i % len(dump_commands)]
                    result = lldb.SBCommandReturnObject()
                    interp.HandleCommand(command, result)
                    worker_results.append(
                        (command, result.Succeeded(), len(result.GetOutput()))
                    )
            except Exception as e:
                worker_exception.append(e)

        worker_thread = threading.Thread(target=dump_worker, daemon=True)
        worker_thread.start()

        # While the worker thread above is (hopefully) busy dumping
        # LLDB's internal Clang AST state, block the main thread for a
        # few seconds inside a JIT'd call to SlowCompute(). This
        # temporarily resumes the inferior and keeps LLDB's private
        # state thread occupied running/waiting on the call.
        self.expect_expr("SlowCompute()", result_type="int", result_value="42")

        # The blocking expression above has now completed. Give the
        # worker thread a bit more time to finish its remaining
        # iterations (it may have been serialized behind the expression
        # evaluation above and still have work left to do), then check
        # that LLDB is still alive and both kinds of command completed
        # successfully.
        worker_thread.join(timeout=60)
        self.assertFalse(worker_thread.is_alive(), "Worker thread should have finished")
        self.assertEqual(
            worker_exception, [], f"Worker thread hit an exception: {worker_exception}"
        )
        self.assertGreater(
            len(worker_results), 0, "Worker thread should have issued some commands"
        )
        for command, succeeded, output_len in worker_results:
            self.assertTrue(
                succeeded, f"Command {command!r} should have succeeded"
            )
            self.assertGreater(
                output_len, 0, f"Command {command!r} should have produced output"
            )

        # LLDB itself must still be alive and responsive after all of
        # this -- issue one final, ordinary dump to confirm the scratch
        # AST context is still usable.
        self.expect("target dump typesystem", substrs=["Widget"])
