"""
  Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
  See https://llvm.org/LICENSE.txt for license information.
  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

A pre-test sanity check for the machine running the test suite.

Before every test the suite compiles a trivial C program (that is unique for
every attempt) and debugs it up to a breakpoint in 'main'. If that fails, the
machine is considered to be in a bad state (e.g., the kernel refuses to launch
or attach to new processes) and the check is retried a few times before giving
up and erroring out the test.

This file is intentionally self-contained so that it doesn't conflict with
upstream changes to the test suite. It is invoked from Base.setUp in
lldbtest.py.

The check can be configured with these environment variables:
  LLDB_SKIP_SANITY_CHECK:         Set to '1' to disable the check entirely.
  LLDB_SANITY_CHECK_ATTEMPTS:     Number of attempts before failing the test.
  LLDB_SANITY_CHECK_RETRY_WAIT:   Seconds to wait between two attempts.
"""

# System modules
import os
import subprocess
import sys
import tempfile
import time
import uuid

# LLDB modules
import lldb
from . import configuration
from . import lldbplatformutil
from . import lldbutil

# Number of times the sanity check is attempted before the test is failed.
DEFAULT_MAX_ATTEMPTS = 10

# Seconds to wait before retrying a failed sanity check.
DEFAULT_RETRY_WAIT = 30.0

# The source of the program that is compiled and debugged. The unique string
# makes sure that neither the compiler nor LLDB can reuse any cached state
# (e.g., a module cache entry) from a previous attempt.
SOURCE_TEMPLATE = """\
volatile const char *random = "@UNIQUE@";
int main() { return 0; }
"""

# Cached result of _get_target_flags.
_cached_target_flags = None


def _env_int(name, default):
    try:
        return int(os.environ[name])
    except (KeyError, ValueError):
        return default


def _env_float(name, default):
    try:
        return float(os.environ[name])
    except (KeyError, ValueError):
        return default


def is_enabled():
    """Returns True if the sanity check should run for this test run."""
    if os.environ.get("LLDB_SKIP_SANITY_CHECK", "0") not in ("0", ""):
        return False
    # The check debugs a locally built and locally launched process, so it is
    # meaningless (and would just fail) when testing a remote target or when
    # cross-compiling for another platform.
    if lldb.remote_platform:
        return False
    if configuration.lldb_platform_name:
        return False
    if lldbplatformutil.getPlatform() != lldbplatformutil.getHostPlatform():
        return False
    return True


def _get_target_flags():
    """Returns the flags needed to compile a program for the host."""
    global _cached_target_flags
    if _cached_target_flags is not None:
        return _cached_target_flags

    flags = []
    if configuration.triple:
        flags += ["--target=" + configuration.triple]
    elif configuration.arch:
        flags += ["-arch", configuration.arch]

    if lldbplatformutil.platformIsDarwin():
        # The compiler that is used for testing is usually a just-built clang
        # that doesn't know where the SDK is, so point it at the host SDK.
        sdk_root = lldbutil.get_xcode_sdk_root(configuration.apple_sdk or "macosx")
        if sdk_root:
            flags += ["-isysroot", sdk_root]

    _cached_target_flags = flags
    return flags


def _compile(compiler, source_dir, unique):
    """Compiles the sanity check program and returns the executable path."""
    source = os.path.join(source_dir, "sanity-check.c")
    exe = os.path.join(source_dir, "sanity-check")
    with open(source, "w") as f:
        f.write(SOURCE_TEMPLATE.replace("@UNIQUE@", unique))

    command = [compiler, "-g", "-O0", "-o", exe, source] + _get_target_flags()
    subprocess.run(command, check=True, capture_output=True, errors="replace")
    return exe


def _debug(exe):
    """Debugs the given executable up to a breakpoint in 'main'.

    Raises an exception if the breakpoint wasn't reached."""
    debugger = lldb.SBDebugger.Create()
    try:
        debugger.SetAsync(False)
        target = debugger.CreateTarget(exe)
        if not target:
            raise Exception("failed to create target for '%s'" % exe)

        breakpoint = target.BreakpointCreateByName("main", exe)
        if breakpoint.GetNumLocations() == 0:
            raise Exception("no locations for the breakpoint on 'main'")

        process = target.LaunchSimple(None, None, os.path.dirname(exe))
        if not process:
            raise Exception("failed to launch '%s'" % exe)

        try:
            state = process.GetState()
            if state != lldb.eStateStopped:
                raise Exception(
                    "process is in state '%s' instead of 'stopped'"
                    % lldb.SBDebugger.StateAsCString(state)
                )
            stopped_at_breakpoint = any(
                thread.GetStopReason() == lldb.eStopReasonBreakpoint
                for thread in process
            )
            if not stopped_at_breakpoint:
                raise Exception("process didn't stop at the breakpoint on 'main'")
        finally:
            process.Kill()
    finally:
        lldb.SBDebugger.Destroy(debugger)


def _attempt(compiler):
    """Runs a single sanity check attempt in a temporary directory."""
    unique = str(uuid.uuid4())
    with tempfile.TemporaryDirectory(prefix="lldb-sanity-check-") as source_dir:
        exe = _compile(compiler, source_dir, unique)
        _debug(exe)


def run(test):
    """Runs the sanity check for the given test.

    Raises an exception (which errors out the test) if the machine still fails
    to debug a trivial program after all attempts have been exhausted."""
    if not is_enabled():
        return

    max_attempts = max(1, _env_int("LLDB_SANITY_CHECK_ATTEMPTS", DEFAULT_MAX_ATTEMPTS))
    retry_wait = _env_float("LLDB_SANITY_CHECK_RETRY_WAIT", DEFAULT_RETRY_WAIT)
    compiler = test.getCompiler()

    for attempt in range(1, max_attempts + 1):
        try:
            _attempt(compiler)
            return
        except Exception as e:
            if isinstance(e, subprocess.CalledProcessError):
                reason = "compiling the program failed: %s\n%s" % (e, e.stderr)
            else:
                reason = "debugging the program failed: %s" % e
            print(
                "Sanity check attempt %d of %d failed, %s"
                % (attempt, max_attempts, reason),
                file=sys.stderr,
            )
            if attempt == max_attempts:
                raise Exception(
                    "The machine failed to build and debug a trivial program "
                    "%d times in a row and seems to be in a bad state. Last "
                    "failure: %s" % (max_attempts, reason)
                )
            print(
                "Waiting %g seconds before retrying the sanity check..." % retry_wait,
                file=sys.stderr,
            )
            time.sleep(retry_wait)
