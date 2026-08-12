import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDeletedDefaultCtorAggregateOdrTestCase(TestBase):
    def test_conflicting_globals_evaluate_fine(self):
        """
        Tests LLDB's behaviour when the same struct name ('Pod') is defined
        with a deleted default constructor in the main executable (making it
        a non-aggregate, even though it is still initialized via
        list-initialization syntax that doesn't need the default
        constructor) and with an implicit, non-deleted, trivial default
        constructor in a dylib (a plain aggregate).

        Evaluating each module's own global, and both together in the same
        expression, should work fine: neither of these needs to call the
        (deleted, in main.cpp's case) default constructor.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("gMainPod.a", result_type="int", result_value="1")
        self.expect_expr("gPluginPod.a", result_type="int", result_value="3")

        # Referencing both modules' conflicting 'Pod' definitions in the
        # same expression forces the ASTImporter to import/complete both
        # into the shared scratch AST context together. This alone doesn't
        # need to call any default constructor, so it should work fine.
        self.expect_expr(
            "gMainPod.a + gPluginPod.a", result_type="int", result_value="4"
        )

    @expectedFailureAll(
        bugnumber="LLDB's DWARF parser never reads DW_AT_deleted, so a "
        "constructor that was declared '= delete' in the source (and for "
        "which the compiler therefore never emitted any code) shows up in "
        "LLDB's Clang AST as an ordinary non-deleted, user-provided, "
        "bodyless constructor. Evaluating an expression that needs to "
        "default-construct such a type therefore doesn't get rejected at "
        "parse time (as real C++ would reject it, with a 'call to deleted "
        "constructor' diagnostic); instead LLDB's expression evaluator "
        "tries to call the (fake, apparently non-deleted) constructor and "
        "fails much later with a confusing link error ('Couldn't look up "
        "symbols'), because the compiler never actually emitted any code "
        "for the truly-deleted constructor."
    )
    def test_default_construct_deleted_ctor(self):
        """
        Stopped in a frame belonging to the main executable (where 'Pod' has
        a *deleted* default constructor), evaluate an expression that
        default-constructs a 'Pod' with no initializer. In real, compiled
        C++ this is ill-formed and rejected at compile time with a "call to
        deleted constructor" diagnostic. LLDB's expression evaluator should
        ideally reject this the same way, since it parsed 'Pod's definition
        (including the deleted default constructor) straight out of the
        same main executable's debug info. Instead, because LLDB's
        DWARF parser drops the "this constructor is deleted" bit entirely,
        the expression gets past parsing and only fails later, while trying
        to link against a constructor symbol that was never emitted into
        the binary.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull the dylib's (non-deleted) 'Pod' into the scratch AST context
        # first, to see whether merge order can make the dylib's non-deleted
        # default constructor "win" for the main executable's same-named
        # (but genuinely deleted) 'Pod'.
        self.expect_expr("gPluginPod.a", result_type="int", result_value="3")

        frame = self.frame()
        self.assertTrue(frame.IsValid())

        # This default-constructs a 'Pod' with no initializer while stopped
        # in a frame belonging to the main executable, forcing name lookup
        # to resolve 'Pod's default constructor against main.cpp's
        # (genuinely deleted) definition. This should be rejected with a
        # "call to deleted constructor" style parse error, the same way
        # real C++ would reject it.
        self.expect(
            "expr Pod p3;",
            error=True,
            substrs=["deleted"],
        )
