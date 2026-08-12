"""
Tests LLDB's behavior when two independent Clang modules (each with its own
PCM, built with -fmodules -gmodules) declare their own, entirely unrelated
"struct Shared" under the very same name.

main.cpp is compiled with only ModuleA's directory on its include/module
search path, so the executable's debug info embeds ModuleA's PCM which
contains its own definition of "struct Shared" (two ints). The "plugin"
dylib is compiled with only ModuleB's directory on its search path, so its
debug info embeds a completely different PCM with an unrelated "struct
Shared" (three doubles) under the same name.

This is a real ODR violation across Clang modules: there is no single
consistent definition for "Shared" anywhere in the (notional) source
program, yet LLDB has to reconcile the two conflicting module-provided
AST nodes into a single scratch AST when asked to look at both globals
in the same expression. This is exactly the kind of input the
ASTImporter/DWARFASTParserClang and Clang's own module deserialization
code are known to handle poorly, and can crash or assert instead of
just reporting an ambiguous/inconsistent type.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstGmodulesConflictingPcmTestCase(TestBase):
    @add_test_categories(["gmodules"])
    def test_conflicting_pcm_shared_struct(self):
        """
        Evaluate expressions that reference the module-provided "Shared"
        globals from both the executable (ModuleA's PCM) and the dylib
        (ModuleB's PCM) in the hope of triggering a crash/assertion while
        LLDB tries to import and merge the two conflicting definitions of
        "struct Shared" into a single scratch AST.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Look at each global on its own first.
        self.expect_expr("gModuleAGlobal")
        self.expect_expr("gModuleBGlobal")

        # Now force LLDB to reconcile both conflicting "Shared" definitions
        # inside a single expression's AST.
        self.expect_expr(
            "(int)sizeof(gModuleAGlobal) + (int)sizeof(gModuleBGlobal)"
        )

        # And exercise "image lookup" which walks the debug-info driven
        # DWARFASTParserClang path directly for the type name that is
        # ambiguous across the two PCMs.
        self.expect("image lookup -t Shared")
