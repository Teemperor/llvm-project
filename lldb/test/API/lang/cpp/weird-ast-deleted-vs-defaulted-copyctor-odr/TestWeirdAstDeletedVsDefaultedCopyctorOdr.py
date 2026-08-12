import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDeletedVsDefaultedCopyctorOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each definition of 'Widget' can be read/printed fine on its own, as
        long as the *other* conflicting definition hasn't already been
        imported into the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("w1.x", result_type="int", result_value="2")
        self.expect_expr("w2.x", result_type="int", result_value="20")

    @expectedFailureAll(
        bugnumber="LLDB's DWARFASTParserClang never reads DW_AT_deleted, so "
        "a copy assignment/copy constructor that is '= delete'd in the "
        "source is reconstructed in LLDB's Clang AST as an ordinary, "
        "non-deleted special member. Evaluating an assignment that should "
        "be ill-formed (because it binds to a deleted operator=) is "
        "instead accepted by Sema and only fails much later, when the "
        "IR interpreter/JIT can't find a body for a function that was "
        "never actually emitted (since it really is deleted in the "
        "binary), instead of being rejected up front as invalid."
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same class ('Widget') has two
        conflicting definitions: one in the main executable, where the copy
        constructor and copy assignment operator are explicitly deleted,
        and one in a dylib, where the same special members are explicitly
        defaulted (i.e. usable). This is a real ODR violation purely in the
        "deleted-ness" of the special members -- the data layout is
        identical on both sides.

        Using both conflicting definitions of 'Widget' together forces
        LLDB's ASTImporter to reconcile two CXXRecordDecls for 'Widget'
        with different isDeleted()/isDefaulted() special members in the
        shared scratch AST context. This tries to invoke the exe's
        (deleted) 'operator=' via the merged type. This should ideally be
        rejected as an error (since the exe's Widget's copy assignment
        really is deleted), but currently isn't.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference both conflicting definitions of 'Widget' in the same
        # debug session, forcing the ASTImporter to reconcile the two
        # conflicting CXXRecordDecls.
        self.expect_expr("w1.x", result_type="int", result_value="2")
        self.expect_expr("w2.x", result_type="int", result_value="20")

        # This assigns through the exe's 'Widget', whose copy assignment
        # operator is '= delete'd. A real C++ compiler would reject this
        # expression at compile time. Assert that LLDB does the same.
        self.expect(
            "expression w1 = w2",
            error=True,
            substrs=["deleted"],
        )
