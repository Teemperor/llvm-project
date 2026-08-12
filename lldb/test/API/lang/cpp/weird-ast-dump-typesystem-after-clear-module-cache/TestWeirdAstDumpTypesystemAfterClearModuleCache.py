"""
Test LLDB's handling of a two-way ODR conflict on the same struct tag
name ('Shape') spread across two dylibs:
  dylib1: struct Shape { int tag; int value; };
  dylib2: struct Shape { double w, h, d; };

The main executable links both dylibs and never itself sees either
definition of 'Shape'. Evaluating whole-struct expressions on each
dylib's global (which creates a persistent '$N' result variable of type
'Shape') forces LLDB's DWARFASTParserClang/ASTImporter machinery to
import both conflicting definitions of the tag name 'Shape' into the
target's shared per-target scratch ASTContext at the same time -- a
real ODR violation.

After that scratch context is in this merged/conflicting state, the
test disables Clang-module auto-import ('settings set
target.auto-import-clang-modules false') and evaluates one more
expression, then -- while the inferior process is still alive --
deletes the entire on-disk directory backing
'symbols.clang-modules-cache-path' out from under LLDB, and immediately
(without re-running any expression) dumps both the scratch typesystem
('target dump typesystem') and the per-module DWARF-derived ASTs
('target modules dump ast --filter Shape').

The motivating worry is that some lazily-computed piece of AST state
(e.g. a cached mapping from on-disk module-cache file paths to mmap'd
PCM data, or metadata on a decl's owning module) might still hold a
pointer into the now-deleted module-cache directory's backing store, so
that dumping decls after the directory disappears could read from
unmapped/freed memory. This test does not assert that such a crash
happens; on this DWARF-only (no explicit Clang modules) code path,
nothing under the module-cache directory is actually referenced by the
dumps, so this pins down the current well-formed, non-corrupted output
-- a regression that started depending on that now-missing directory
would show up as a change in (or crash instead of) the dumped output.
"""

import os

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpTypesystemAfterClearModuleCacheTestCase(TestBase):
    def test(self):
        """
        Import both conflicting 'Shape' definitions into the shared
        scratch ASTContext, disable Clang-module auto-import, then
        delete the on-disk Clang-module cache directory while the
        process is still live, and finally dump both the scratch
        typesystem and the per-module ASTs without re-running any
        expression.
        """
        self.build()

        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("main.cpp")
        )

        # (1) Import dylib1's 'struct Shape { int tag; int value; }'
        # into the shared scratch ASTContext by creating a persistent
        # result variable of that type.
        self.expect_expr(
            "gShape1", result_type="Shape", result_children=[ValueCheck(name="tag")]
        )

        # (2) Import dylib2's mutually-incompatible
        # 'struct Shape { double w, h, d; }' into the very same scratch
        # ASTContext.
        self.expect_expr(
            "gShape2", result_type="Shape", result_children=[ValueCheck(name="w")]
        )

        # (3) Read a field off of dylib1's global, exercising the
        # merged/conflicting scratch state.
        self.expect_expr("gShape1.tag", result_type="int", result_value="1")

        # Disable Clang-module auto-import, then evaluate one more
        # expression before touching the module-cache directory.
        self.runCmd("settings set target.auto-import-clang-modules false")
        self.expect_expr("gShape2.h", result_type="double", result_value="2.5")

        # Find out where the on-disk Clang-module cache lives, then
        # delete it out from under LLDB while the inferior process is
        # still alive.
        module_cache_path = self.getModuleCacheDir()
        self.assertTrue(
            len(module_cache_path) > 0,
            "expected a non-empty symbols.clang-modules-cache-path",
        )
        lldbutil.remove_tree(module_cache_path)
        self.assertFalse(
            os.path.isdir(module_cache_path),
            "module cache directory should be gone now",
        )

        # Immediately dump the scratch typesystem and the per-module
        # ASTs -- without re-running any expression -- while the
        # process is still live and the module-cache directory is
        # missing. Both mutually-incompatible 'Shape' RecordDecls
        # should still show up, well-formed, in the scratch dump.
        self.expect(
            "target dump typesystem",
            substrs=[
                "struct Shape definition",
                "tag 'int'",
                "value 'int'",
                "w 'double'",
                "h 'double'",
                "d 'double'",
            ],
        )

        self.expect(
            "target modules dump ast --filter Shape",
            patterns=[r"struct Shape definition"] * 2,
        )

        process.Continue()

    def getModuleCacheDir(self):
        interp = self.dbg.GetCommandInterpreter()
        result = lldb.SBCommandReturnObject()
        interp.HandleCommand("settings show symbols.clang-modules-cache-path", result)
        self.assertTrue(result.Succeeded())
        output = result.GetOutput()
        # Output looks like:
        #   symbols.clang-modules-cache-path (file) = "/some/path"
        start = output.find('"')
        end = output.rfind('"')
        self.assertTrue(start != -1 and end != -1 and end > start)
        return output[start + 1 : end]
