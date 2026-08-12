"""
Test LLDB's behaviour when the same-named, same-layout class 'Box' has
'operator==' implemented as a befriended free function in the main
executable, but as a genuine member function in a dylib.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFriendVsMemberOdrTestCase(TestBase):
    def test_box_alone_in_main_exe(self):
        """
        Looking at the main executable's 'Box' (whose 'operator==' is a
        free function befriended via a FriendDecl) on its own should
        work fine, before the dylib's conflicting 'Box' (with a member
        'operator==') is ever imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("box1 == box1", result_type="bool", result_value="true")

    def test_box_alone_in_dylib(self):
        """
        Looking at the dylib's 'Box' (whose 'operator==' is a genuine
        member function) on its own should also work fine, before the
        main executable's conflicting 'Box' (with its befriended free
        'operator==') is ever imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("box2 == box2", result_type="bool", result_value="true")
        self.expect_expr(
            "box2.operator==(box2)", result_type="bool", result_value="true"
        )

    def test_dump_ast_after_importing_conflicting_box_definitions(self):
        """
        Tests LLDB's behaviour when the same-named, same-layout 'Box' is:
          - defined in the main executable with 'operator==' as a free
            function befriended via a FriendDecl (found via
            namespace-scope/ADL lookup), and
          - defined in a dylib with the identical field layout, but with
            'operator==' as a genuine member function (a direct
            CXXMethodDecl child of Box's DeclContext, found via
            class-scope lookup).

        This is an ODR violation: the same-named type has two
        incompatible shapes for how its comparison operator is attached
        to the DeclContext across translation units. After evaluating
        expressions that pull each module's conflicting 'Box'
        CXXRecordDecl into the target's shared scratch AST context (via
        DWARFASTParserClang/ASTImporter), dumping a module's Clang AST
        and the shared scratch typesystem forces LLDB's ASTPrinter and
        ASTImporter machinery to traverse the inconsistent
        DeclContext/StoredDeclsMap state produced by these conflicting
        'Box' definitions. The hope is that this crashes LLDB outright
        (e.g. an assertion or segfault inside Sema::LookupOperatorOverloads
        while computing implicit-object-argument conversions, or inside
        Clang's RecursiveASTVisitor) instead of merely producing a
        wrong-but-well-formed value.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull the main executable's friend-operator 'Box' into the
        # scratch AST context.
        self.expect_expr("box1 == box1", result_type="bool", result_value="true")

        # Now pull the dylib's member-operator, ODR-conflicting 'Box'
        # into the same scratch AST context.
        self.expect_expr("box2 == box2", result_type="bool", result_value="true")

        # Dumping each module's AST filtered by 'Box' still has to
        # traverse both conflicting 'Box' CXXRecordDecls, one carrying a
        # FriendDecl-attached free 'operator==', the other a direct
        # CXXMethodDecl 'operator==' child. This should never crash
        # LLDB, no matter how inconsistent the merged
        # DeclContext/StoredDeclsMap bookkeeping for 'Box' has become.
        self.expect("target modules dump ast --filter Box")

        # Dumping the shared scratch typesystem after both conflicting
        # 'Box' definitions have been referenced should also never
        # crash LLDB.
        self.expect("target dump typesystem")

    @expectedFailureAll(
        bugnumber="mixing friend-operator Box with member-operator Box makes operator== ambiguous"
    )
    def test_cross_module_operator_lookup_is_ambiguous(self):
        """
        Documents a real limitation: once the scratch AST context has
        seen both the main executable's friend-operator 'Box' and the
        dylib's member-operator 'Box', Clang overload resolution for
        'operator==' becomes ambiguous even for uses that, taken in
        isolation within a single module, are perfectly well-formed
        (e.g. comparing the dylib's own 'box2' against itself). This
        happens because Sema::LookupOperatorOverloads ends up
        considering both the free, befriended 'operator==' and the
        member 'operator==' as candidates for what LLDB believes is a
        single merged 'Box' type.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # First make sure both conflicting 'Box' shapes have been pulled
        # into the scratch AST context.
        self.expect_expr("box1 == box1", result_type="bool", result_value="true")
        self.expect_expr("box2 == box2", result_type="bool", result_value="true")

        # Now that both conflicting 'Box' definitions have been merged,
        # comparing the two globals directly should still produce a
        # well-formed result instead of an ambiguous-overload error.
        self.expect_expr("box1 == box2", result_type="bool", result_value="true")

    def test_repeated_ambiguous_expressions_and_dumps_do_not_crash(self):
        """
        Repeatedly evaluates expressions that mix both conflicting 'Box'
        definitions, interleaved with AST/typesystem dumps, to
        stress-test the ASTImporter/DWARFASTParserClang machinery for a
        crash under repeated re-entry rather than a single one-shot
        evaluation.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        for _ in range(3):
            # These are ambiguous-overload errors (see
            # test_cross_module_operator_lookup_is_ambiguous above), but
            # they should never crash LLDB, no matter how many times
            # they are repeated.
            self.expect("expr box1 == box2", error=True)
            self.expect("expr box2 == box1", error=True)
            self.expect("target modules dump ast --filter Box")
            self.expect("expr box2.operator==(box2)")
            self.expect("target dump typesystem")
