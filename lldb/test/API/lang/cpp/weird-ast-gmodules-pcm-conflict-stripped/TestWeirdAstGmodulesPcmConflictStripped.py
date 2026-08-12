"""
Tests LLDB's behavior when a main executable and a dylib each embed their
own -fmodules/-gmodules Clang module named "Shapes", both defining a
"struct Circle" under the same name but with incompatible layouts (the
dylib's "Circle" has an extra 'area' field), and the dylib's DWARF is then
stripped away entirely with 'strip -S'.

main.cpp only ever sees ModuleA's "Shapes" module (via its own
per-translation-unit module search path -- see Makefile), so the
executable's debug info embeds a PCM in which "struct Circle" is just a
radius. plugin.cpp only ever sees ModuleB's incompatible "Shapes" module,
so its (now-stripped) debug info would have embedded a completely
different, differently-hashed PCM with an unrelated "struct Circle" (radius
plus an 'area' field) under the very same module and type name.

Because the dylib's DWARF is gone, DWARFASTParserClang has nothing left to
parse for that image: there is no debug info anywhere in the debug session
that still imports/uses ModuleB's conflicting .pcm. This is exactly the
kind of "the only side of an ODR violation that's left is a dangling
module-cache reference with no matching debug info" state that the
ASTImporter, TypeSystemClang and Clang's own module deserialization code
are not necessarily built to handle gracefully, and is a plausible place
to find a real crash/assertion instead of a merely wrong-but-well-formed
answer.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstGmodulesPcmConflictStrippedTestCase(TestBase):
    @add_test_categories(["gmodules"])
    def test_gmodules_pcm_conflict_stripped_dylib(self):
        """
        Evaluate expressions that reference both the executable's
        module-provided "Circle" global (from ModuleA's PCM) and the
        stripped dylib's "Circle" global (whose only complete definition
        ever lived in ModuleB's now-unreachable, conflicting PCM), in the
        hope of triggering a crash/assertion while LLDB tries to reconcile
        -- or simply give up on -- the two conflicting definitions of
        "struct Circle" sharing the same module name.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Look at each global on its own first. Since the dylib's DWARF is
        # gone, gPluginCircle's type can no longer be resolved via debug
        # info at all; the goal here is simply that LLDB doesn't crash
        # while trying.
        self.expect("target variable gMainCircle")
        self.expect("target variable gPluginCircle")
        self.expect("expression gMainCircle")
        self.expect("expression gPluginCircle")

        # Now force LLDB to reconcile whatever it thinks both globals'
        # types are inside a single expression's AST.
        self.expect("expression (int)sizeof(gMainCircle) + (int)sizeof(gPluginCircle)")

        # "image lookup" walks the debug-info-driven DWARFASTParserClang
        # path directly for the type name that used to be ambiguous across
        # the two PCMs (now that the dylib's copy is gone).
        self.expect("image lookup -t Circle -A")

        # Dump the per-module Clang ASTs (DWARF-derived) and the shared
        # scratch TypeSystem/ASTContext that expression evaluation and the
        # ASTImporter actually import decls into. This is the most
        # promising place to look for corruption after LLDB tries (and
        # partially fails) to make sense of "Circle" across the two
        # modules.
        self.expect("target modules dump ast --filter Circle")
        self.expect("target dump typesystem")

        # Repeat the ambiguous expression once more after the AST dumps
        # above, in case dumping itself perturbs the scratch AST.
        self.expect("expression gMainCircle")
