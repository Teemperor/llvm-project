"""
Tests LLDB's robustness when several host-side threads hammer the same
target's shared, per-target scratch TypeSystemClang/ASTContext at once,
with no synchronization between them:

  * Two threads repeatedly run 'target dump typesystem', which walks
    (via clang::RecursiveASTVisitor-based ASTDumper/ASTPrinter machinery)
    every Decl currently sitting in the scratch ASTContext.
  * Two more threads repeatedly run 'target modules dump ast --filter
    Shape' / 'target modules dump ast', which walks the *per-module* AST
    parsed from DWARF (a separate, but related, TypeSystemClang).
  * A fifth thread repeatedly runs 'expression' commands that alternate
    between

        expr typedef int MyInt; MyInt xx = (int)1; xx
        expr typedef float MyInt; MyInt xx = (float)1; xx

    i.e. two back-to-back expression evaluations that each declare a
    *new*, ODR-conflicting top-level typedef named 'MyInt' (first as
    'int', then as 'float') directly inside the shared scratch
    ASTContext, and force LLDB's ClangExpressionDeclMap /
    ClangASTImporter machinery to create and import fresh Decls (and
    IdentifierInfo/DenseMap lookup-table entries) into that same context
    while the other four threads are concurrently iterating over it.

None of TypeSystemClang's scratch ASTContext, clang::ASTContext's
DenseMap-based lookup tables (IdentifierTable, DeclContext lookup maps),
nor LLDB's RecursiveASTVisitor-based dump/print machinery are documented
or designed to support this kind of unsynchronized concurrent
read-while-write access from multiple host threads. This test is a
deliberate, unsynchronized race between "iterate everything in the
scratch AST" and "mutate the scratch AST by declaring new, conflicting
top-level typedefs", run for several seconds, to see whether that race
can crash LLDB itself (as opposed to merely producing a wrong-but
well-formed answer).

This test only asserts that the run completes and that lldb did not
crash (a hard crash kills the whole test process, so a graceful,
non-zero-exceptions-just-recorded completion of build()/run_to_source
_breakpoint() and the loop below is itself the meaningful check). It
deliberately does not make any assertion about the *content* of any
individual dump or expression result -- under a genuine data race
absolutely anything well-formed-looking is an acceptable outcome; the
only unacceptable outcome is a crash of the lldb process running the
test.
"""

import threading

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTypesystemDumpConcurrentThreadsTestCase(TestBase):
    # How long (in seconds) to let the racing threads hammer the shared
    # scratch ASTContext for. Long enough to give the race a realistic
    # chance to hit, short enough to keep the test suite fast.
    RACE_DURATION_SECONDS = 10

    def test(self):
        self.build()

        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        debugger = self.dbg
        # Concurrent SBCommandInterpreter.HandleCommand calls below are
        # much more likely to actually overlap in synchronous mode: in
        # async mode each HandleCommand call could get serialized behind
        # event-handling in ways that mask the race we are trying to hit.
        debugger.SetAsync(False)
        ci = debugger.GetCommandInterpreter()

        # Sanity check: a single, non-concurrent dump works and mentions
        # the type we expect to see, before we throw concurrency at it.
        self.expect("target dump typesystem", substrs=["Shape"])
        self.expect("target modules dump ast --filter Shape", substrs=["Shape"])

        stop_flag = threading.Event()
        # Each worker records the number of iterations it completed and
        # any *graceful* (non-crashing) Python-level exception it saw, so
        # a failure in the harness itself is still visible even if lldb
        # never actually segfaults/aborts.
        counters = {}
        errors = []
        errors_lock = threading.Lock()

        def record_error(name, iteration, exc):
            with errors_lock:
                errors.append((name, iteration, repr(exc)))

        def dump_worker(name, command):
            iterations = 0
            while not stop_flag.is_set():
                result = lldb.SBCommandReturnObject()
                try:
                    ci.HandleCommand(command, result)
                except Exception as exc:  # noqa: BLE001 - see docstring above
                    record_error(name, iterations, exc)
                iterations += 1
            counters[name] = iterations

        def expr_worker(name):
            iterations = 0
            while not stop_flag.is_set():
                if iterations % 2 == 0:
                    command = "expression typedef int MyInt; MyInt xx = (int)1; xx"
                else:
                    command = (
                        "expression typedef float MyInt; MyInt xx = (float)1; xx"
                    )
                result = lldb.SBCommandReturnObject()
                try:
                    ci.HandleCommand(command, result)
                except Exception as exc:  # noqa: BLE001 - see docstring above
                    record_error(name, iterations, exc)
                iterations += 1
            counters[name] = iterations

        workers = [
            threading.Thread(
                target=dump_worker,
                args=("dump_typesystem_1", "target dump typesystem"),
            ),
            threading.Thread(
                target=dump_worker,
                args=("dump_typesystem_2", "target dump typesystem"),
            ),
            threading.Thread(
                target=dump_worker,
                args=(
                    "dump_ast_filtered",
                    "target modules dump ast --filter Shape",
                ),
            ),
            threading.Thread(
                target=dump_worker,
                args=("dump_ast_unfiltered", "target modules dump ast"),
            ),
            threading.Thread(target=expr_worker, args=("conflicting_typedef_expr",)),
        ]

        for worker in workers:
            # Daemon threads: if the harness process itself is about to
            # crash (the whole point of this test), we do not want a
            # non-daemon worker thread to prevent process teardown.
            worker.daemon = True
            worker.start()

        stop_flag.wait(self.RACE_DURATION_SECONDS)
        stop_flag.set()
        for worker in workers:
            worker.join(timeout=30)

        # If we get here at all, lldb survived several seconds of
        # unsynchronized concurrent reads (dumps) and writes (conflicting
        # typedef declarations) against its shared scratch ASTContext
        # without crashing the process running this test. Make sure every
        # worker thread actually made forward progress (a worker that
        # silently deadlocked instead of crashing or completing would
        # otherwise pass this test unnoticed via the join() timeout
        # above).
        for worker_name in (
            "dump_typesystem_1",
            "dump_typesystem_2",
            "dump_ast_filtered",
            "dump_ast_unfiltered",
            "conflicting_typedef_expr",
        ):
            self.assertIn(worker_name, counters)
            self.assertGreater(
                counters[worker_name],
                0,
                "worker %s made no progress at all -- likely deadlocked "
                "instead of racing" % worker_name,
            )

        # A handful of individual HandleCommand calls returning a
        # graceful Python-level error is acceptable (e.g. a transient
        # "no matching type" style diagnostic while a conflicting
        # typedef import is only half complete); it is not the crash
        # this test is hunting for. Only complain if *every single*
        # racing command failed, which would indicate the harness itself
        # is broken rather than having found (or not found) a race.
        total_iterations = sum(counters.values())
        self.assertGreater(
            total_iterations,
            len(errors),
            "every single racing command raised a Python-level "
            "exception; the test harness itself looks broken: %r"
            % (errors[:10],),
        )

        # Finally, confirm the debugger/target/process are still in a
        # coherent, queryable state after the race -- if the scratch
        # ASTContext got corrupted, but not so badly as to crash us
        # outright, a final, non-concurrent dump immediately afterwards
        # is a reasonable place for that corruption to surface as a
        # graceful failure instead.
        self.expect("target dump typesystem")
        self.expect("target modules dump ast --filter Shape")
