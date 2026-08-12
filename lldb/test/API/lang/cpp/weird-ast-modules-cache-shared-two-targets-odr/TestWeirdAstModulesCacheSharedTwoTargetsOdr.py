"""
Tests LLDB's behavior when two entirely separate SBTarget instances --
each for its own executable, exe1 and exe2 -- are loaded into the very
same SBDebugger, both built with -fmodules -gmodules importing a Clang
module with the exact same name ("CommonMod") and the exact same header
name ("Common.h"), but from two different, incompatible header search
paths: exe1's "CommonMod" defines "struct Cfg { int mode; };" while
exe2's defines "struct Cfg { int mode; int flags; };".

Normally each SBTarget gets its own, independent scratch TypeSystemClang,
so this by itself would not be observable. What makes this interesting is
that this test forces both targets to additionally share the exact same
on-disk clang-module-cache directory (via
"settings set symbols.clang-modules-cache-path", configured once, before
either target is created), so any on-disk PCM that LLDB reuses or
re-validates for one target's ASTImporter could in principle be a stale
or colliding PCM that was actually produced for (or is being concurrently
used by) the other target.

The test runs expressions that reference the module-provided "Cfg" global
in both targets, interleaved, then destroys the first target
(SBDebugger.DeleteTarget) and finally runs "target dump typesystem" for
the second, surviving target, in the hope that a cross-target dangling
ASTContext/Module reference surfaces once the first target's scratch
AST and Clang module state have actually been torn down.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstModulesCacheSharedTwoTargetsOdrTestCase(TestBase):
    def _configure_shared_module_cache(self):
        # Force both targets' Clang module (re-)parsing at debug time to
        # go through the exact same on-disk PCM cache directory, before
        # either target has been created.
        mod_cache = self.getBuildArtifact("shared-clang-modules-cache")
        self.runCmd(
            'settings set symbols.clang-modules-cache-path "%s"' % mod_cache
        )
        self.runCmd("settings set target.auto-import-clang-modules true")

    @add_test_categories(["gmodules"])
    def test_shared_module_cache_two_targets_odr(self):
        """
        Create two targets (exe1, exe2) in the same SBDebugger, both
        pointed at the same shared on-disk clang-modules-cache-path, and
        interleave expression evaluation between them before tearing the
        first one down.
        """
        self.build()
        self._configure_shared_module_cache()

        (target1, process1, thread1, bkpt1) = lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main1.cpp"), exe_name="exe1"
        )
        (target2, process2, thread2, bkpt2) = lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main2.cpp"), exe_name="exe2"
        )

        self.assertEqual(self.dbg.GetNumTargets(), 2)

        frame1 = thread1.GetSelectedFrame()
        frame2 = thread2.GetSelectedFrame()

        # Interleave expression evaluation between both targets' "Cfg"
        # globals/locals, forcing each target's own ASTImporter to import
        # its own (mutually incompatible) "Cfg" from debug info/Clang
        # modules while the other target's identically-named module is
        # also live in the very same shared on-disk cache.
        self.dbg.SetSelectedTarget(target1)
        self.expect(
            "expr -- gCfg1",
            substrs=["mode = 11"],
        )
        value2 = frame2.EvaluateExpression("gCfg2")
        self.assertTrue(value2.IsValid())
        self.expect(
            "expr -- gCfg1.mode",
            substrs=["11"],
        )
        self.dbg.SetSelectedTarget(target2)
        self.expect(
            "expr -- gCfg2",
            substrs=["mode = 22", "flags = 222"],
        )

        value_local1 = frame1.EvaluateExpression("local1")
        self.assertTrue(value_local1.IsValid())
        value_local2 = frame2.EvaluateExpression("local2")
        self.assertTrue(value_local2.IsValid())

        # Dump both targets' per-target scratch AST before tearing
        # anything down, to snapshot their (still independent) state.
        self.dbg.SetSelectedTarget(target1)
        self.runCmd("target dump typesystem")
        self.dbg.SetSelectedTarget(target2)
        self.runCmd("target dump typesystem")

        # Now destroy the first target while the second one is still
        # alive. If the shared on-disk module cache ever caused the two
        # targets' ASTImporters/Clang module state to become entangled,
        # this is where a cross-target dangling-context dereference would
        # be expected to surface.
        self.dbg.DeleteTarget(target1)
        self.assertEqual(self.dbg.GetNumTargets(), 1)

        # Exercise the surviving target's expression evaluator and
        # scratch-AST dumping machinery once more, now that the other
        # target (and its scratch TypeSystemClang/ASTContext) is gone.
        self.dbg.SetSelectedTarget(target2)
        self.expect(
            "expr -- gCfg2",
            substrs=["mode = 22", "flags = 222"],
        )
        self.expect(
            "expr -- (int)sizeof(gCfg2)",
        )
        self.expect("target dump typesystem", substrs=["Cfg"])
        self.expect("target modules dump ast --filter Cfg", substrs=["Cfg"])
