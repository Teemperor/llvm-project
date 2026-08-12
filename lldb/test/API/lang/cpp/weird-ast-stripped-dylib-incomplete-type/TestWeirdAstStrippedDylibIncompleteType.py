import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstStrippedDylibIncompleteTypeTestCase(TestBase):
    def test(self):
        """
        Tests evaluating expressions against a type ('struct Handle') whose
        only complete definition lived in a dylib's debug info, which has
        since been stripped (via a post-link 'strip -S' step). The main
        executable only ever contains a forward declaration of Handle plus
        a 'Handle *' global that points at an instance created inside the
        dylib.

        This means that, at debug time, there is no module (main
        executable, dylib or dSYM) anywhere in the debug session that has a
        complete definition of Handle. LLDB is nevertheless forced to try
        to resolve/complete the type when the global pointer is
        dereferenced and its members are accessed. This is meant to stress
        the type-completion fallback paths in
        TypeSystemClang::CompleteType()/DWARFASTParserClang (e.g. the
        "search other compile units for a complete definition" path) which
        may assume a definition can always eventually be found.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # None of these need to succeed with a "correct" answer -- the goal
        # is simply that LLDB doesn't crash while trying to complete/use a
        # type that can never be completed.
        self.expect("target variable g_handle")
        self.expect("expression g_handle")
        self.expect("expression *g_handle")
        self.expect("expression g_handle->id")

        # Calling Handle::getId() requires a JIT call into the member
        # function, but its code (and symbol) were removed by the dylib's
        # post-link 'strip -S' step. There is no way for LLDB to resolve a
        # call to code that no longer exists anywhere in the debug session,
        # so the expected (and correct) behavior is a graceful failure with
        # a helpful diagnostic, rather than a successful evaluation.
        self.expect(
            "expression g_handle->getId()",
            error=True,
            substrs=["Couldn't look up symbols"],
        )
