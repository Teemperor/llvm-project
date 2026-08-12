import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDefaultedMoveVsDeletedCopyOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each definition of 'Resource' can be used correctly on its own, as
        long as the *other* conflicting definition hasn't already been
        imported into the shared per-target scratch AST context.

        The exe's 'Resource' has a usable (defaulted) move constructor and
        no copy constructor at all (implicitly deleted, because declaring a
        move constructor suppresses it). Moving it should work.

        The dylib's 'Resource' has a usable (defaulted) copy constructor
        and no move constructor at all (implicitly deleted, because
        declaring a copy constructor suppresses it). Copying it should
        work.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "Resource moved((Resource&&)r1); moved.h.p", result_type="int *"
        )
        self.expect_expr("Resource copied(r2); copied.h.p", result_type="int *")

    @expectedFailureAll(
        bugnumber="LLDB's ASTImporter merges the exe's and dylib's "
        "conflicting 'Resource' CXXRecordDecls into a single decl in the "
        "shared scratch AST context by matching up special members with "
        "the same signature -- but the exe's 'Resource' only has a move "
        "constructor and the dylib's 'Resource' only has a copy "
        "constructor, so nothing is there for those to merge WITH, and "
        "the merged decl ends up Frankensteined together with BOTH a "
        "move constructor AND a copy constructor, even though neither "
        "original definition actually has both. As a result, moving the "
        "dylib's 'Resource' (whose move constructor is really "
        "implicitly-deleted, since it declares a copy constructor) is "
        "incorrectly accepted by Sema using the exe's move constructor, "
        "instead of being rejected the same way copying the exe's "
        "'Resource' correctly is"
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same class ('Resource') has
        two conflicting definitions that swap which special member is
        defaulted vs. implicitly deleted: the exe's 'Resource' defaults its
        move constructor (and has no copy constructor), while the dylib's
        'Resource' defaults its copy constructor (and has no move
        constructor). Both have identical layout (a single 'Handle h'
        member), so this is a pure "which special member exists"/ODR
        conflict, not a layout conflict.

        Using both conflicting definitions of 'Resource' together forces
        LLDB's ASTImporter to reconcile two CXXRecordDecls for 'Resource'
        whose sets of user-declared special members don't line up at all.
        This then tries to move-construct from the dylib's 'Resource',
        whose move constructor really is implicitly deleted. A real C++
        compiler would reject this expression at compile time (the same
        way it rejects copy-constructing the exe's 'Resource', which this
        test also checks). Assert that LLDB does the same for both.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference both conflicting definitions of 'Resource' in the same
        # debug session, forcing the ASTImporter to reconcile the two
        # conflicting CXXRecordDecls.
        self.expect_expr(
            "Resource moved((Resource&&)r1); moved.h.p", result_type="int *"
        )
        self.expect_expr("Resource copied(r2); copied.h.p", result_type="int *")

        # This move-constructs from the dylib's 'Resource', whose move
        # constructor is really implicitly deleted (because it declares a
        # copy constructor). A real C++ compiler would reject this the same
        # way it rejects copy-constructing the exe's 'Resource' (checked
        # below). Assert that LLDB does the same.
        self.expect(
            "expression Resource illegal_move((Resource&&)r2)",
            error=True,
            substrs=["deleted"],
        )

        # Sanity check: copy-constructing the exe's 'Resource' (whose copy
        # constructor really is implicitly deleted) is correctly rejected.
        self.expect(
            "expression Resource illegal_copy(r1)",
            error=True,
            substrs=["deleted"],
        )
