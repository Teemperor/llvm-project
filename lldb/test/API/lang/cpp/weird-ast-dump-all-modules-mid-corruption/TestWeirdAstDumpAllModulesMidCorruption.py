import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpAllModulesMidCorruptionTestCase(TestBase):
    def test_dump_all_modules_after_odr_merge(self):
        """
        Tests LLDB's handling of two dylibs that each define a
        same-named, similarly-shaped, but ODR-conflicting 'Shared'
        struct: dylib1's 'Shared' has a single 'int' member 'a', while
        dylib2's 'Shared' has a single 'float' member 'a'. Evaluating
        'dylib1_ptr->a + dylib2_ptr->a' in a *single* expression forces
        the expression evaluator to resolve both conflicting 'Shared'
        completions (to read each 'a' member) and compute a common type
        for the '+' operator, all within the shared per-target scratch
        TypeSystemClang/ASTContext that the ASTImporter imports into.

        Immediately afterward, without evaluating any further
        expressions, this runs 'target modules dump ast' with *no*
        module argument -- so it dumps the Clang AST for every loaded
        module (the main executable, dylib1, and dylib2) in one shot --
        and separately 'target dump typesystem' to dump the shared
        scratch AST context. The all-modules dump path walks and
        (re-)completes many CUs' RecordDecls back to back while the
        scratch TypeSystemClang may still be holding the half-merged
        state left over from reconciling the conflicting 'Shared'
        completions; if that traversal calls isCompleteDefinition or
        getASTRecordLayout on the corrupted merged type from a code path
        that lacks whatever guards expression evaluation happens to have,
        it could crash. At a minimum, both commands must complete
        without crashing LLDB, regardless of whether the type information
        they print about the conflicting 'Shared' definitions is
        faithful to either dylib.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Force the expression evaluator to resolve both dylibs'
        # conflicting 'Shared' completions (int-typed 'a' vs. float-typed
        # 'a') in a single expression, which is what pulls both into the
        # shared per-target scratch AST context together (rather than one
        # at a time in separate, unrelated expressions).
        self.expect("expression gShared1->a + gShared2->a")

        # With no module argument, this dumps the Clang AST for every
        # loaded module -- the main executable, dylib1, and dylib2 -- in
        # one shot, immediately after the merge above and without any
        # further expression evaluation in between.
        self.expect("target modules dump ast")

        # Separately, dump the shared per-target scratch
        # TypeSystem/ASTContext that the expression above actually
        # imported the conflicting 'Shared' completions into.
        self.expect("target dump typesystem", substrs=["Shared"])

        # Exercise the merged state a bit more and dump again, in case
        # corruption only shows up after further use.
        self.expect("expression gShared2->a + gShared1->a")
        self.expect("target modules dump ast")
        self.expect("target dump typesystem", substrs=["Shared"])
