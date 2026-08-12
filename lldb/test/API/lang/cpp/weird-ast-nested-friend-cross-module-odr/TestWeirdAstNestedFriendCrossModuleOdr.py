"""
Test LLDB's behaviour when two dylibs each define a same-named, nested
class 'Outer::Inner' with incompatible shapes: one dylib's 'Inner' is a
'class' that befriends its enclosing 'Outer' (granting access to a
private field via a FriendDecl attached to Inner's DeclContext), while
the other dylib's 'Inner' is a plain 'struct' with no friend declaration
and an extra field.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstNestedFriendCrossModuleOdrTestCase(TestBase):
    def test_a_outer_alone(self):
        """
        Looking at DylibOne's 'Outer' (whose nested 'Inner' is a 'class'
        that befriends 'Outer') on its own should work fine, before
        DylibTwo's conflicting 'Outer' is ever imported into the scratch
        AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "nested_friend_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("a_outer->make().secret", result_type="int", result_value="1")

    def test_b_outer_alone(self):
        """
        Looking at DylibTwo's 'Outer' (whose nested 'Inner' is a plain
        'struct' with an extra field and no friend declaration) on its
        own should also work fine, before DylibOne's conflicting 'Outer'
        is ever imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "nested_friend_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("b_outer->make().secret", result_type="int", result_value="2")
        self.expect_expr("b_outer->make().extra", result_type="double", result_value="3.5")

    def test_dump_ast_after_importing_conflicting_outer_definitions(self):
        """
        Tests LLDB's behaviour when the same qualified nested-type name
        'Outer::Inner' is:
          - defined in DylibOne as a 'class' that befriends its
            enclosing 'Outer' (a FriendDecl attached to Inner's
            DeclContext), and
          - defined in DylibTwo as a 'struct' with no friend declaration
            and an extra 'double' field.

        This is an ODR violation with a class/struct tag-kind mismatch
        plus a differing friend graph for the same nested-class name.
        After evaluating expressions that pull each dylib's conflicting
        'Outer'/'Outer::Inner' CXXRecordDecls into the target's shared
        scratch AST context (via DWARFASTParserClang/ASTImporter),
        dumping a module's Clang AST and the shared scratch typesystem
        forces LLDB's ASTPrinter and ASTImporter machinery to traverse
        the merged (and structurally inconsistent) 'Outer' DeclContext,
        which now lexically contains two differently-shaped 'Inner'
        nested classes. The hope is that this crashes LLDB outright
        (e.g. an assertion inside the ASTImporter's structural
        equivalence check, or inside Clang's RecursiveASTVisitor while
        walking Outer's nested decls) instead of merely producing a
        wrong-but-well-formed value.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "nested_friend_entry", lldb.SBFileSpec("main.cpp")
        )

        # Pull DylibOne's friended-class 'Outer'/'Outer::Inner' into the
        # scratch AST context.
        self.expect_expr("a_outer->make().secret", result_type="int", result_value="1")

        # Now pull DylibTwo's ODR-conflicting struct-'Inner' 'Outer' into
        # the same scratch AST context.
        self.expect_expr("b_outer->make().secret", result_type="int", result_value="2")

        # Dumping each module's AST filtered by 'Outer' still has to
        # traverse both conflicting 'Outer' CXXRecordDecls, each of which
        # lexically contains a differently-shaped nested 'Inner'. This
        # should never crash LLDB, no matter how inconsistent the merged
        # DeclContext bookkeeping for 'Outer'/'Outer::Inner' has become.
        self.expect("target modules dump ast --filter Outer")

        # Dumping the shared scratch typesystem after both conflicting
        # 'Outer' definitions have been referenced should also never
        # crash LLDB.
        self.expect("target dump typesystem")

    @expectedFailureAll(
        bugnumber="mixing class-Inner-with-friend Outer with struct-Inner Outer makes make() ambiguous"
    )
    def test_cross_module_member_call_is_ambiguous(self):
        """
        Documents a real limitation: once the scratch AST context has
        seen both DylibOne's friended-class-Inner 'Outer' and DylibTwo's
        struct-Inner 'Outer', calling 'make()' in a single expression
        that references both globals becomes ambiguous, even though each
        call is perfectly well-formed when evaluated on its own (see
        test_a_outer_alone/test_b_outer_alone above). This happens
        because LLDB's expression parser ends up considering 'make()'
        CXXMethodDecls from two distinct (but same-named/merged) 'Outer'
        CXXRecordDecls as candidates for a single call.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "nested_friend_entry", lldb.SBFileSpec("main.cpp")
        )

        # First make sure both conflicting 'Outer' shapes have been
        # pulled into the scratch AST context.
        self.expect_expr("a_outer->make().secret", result_type="int", result_value="1")
        self.expect_expr("b_outer->make().secret", result_type="int", result_value="2")

        # Referencing both dylibs' 'Outer::make()' in the same expression
        # should still produce a well-formed result instead of an
        # ambiguous-overload error.
        self.expect_expr(
            "a_outer->make().secret + b_outer->make().secret",
            result_type="int",
            result_value="3",
        )

    def test_repeated_ambiguous_expressions_and_dumps_do_not_crash(self):
        """
        Repeatedly evaluates expressions that mix both conflicting
        'Outer'/'Outer::Inner' definitions, interleaved with AST/
        typesystem dumps, to stress-test the
        ASTImporter/DWARFASTParserClang machinery for a crash under
        repeated re-entry rather than a single one-shot evaluation.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "nested_friend_entry", lldb.SBFileSpec("main.cpp")
        )

        for _ in range(3):
            self.expect_expr(
                "a_outer->make().secret", result_type="int", result_value="1"
            )
            self.expect_expr(
                "b_outer->make().secret", result_type="int", result_value="2"
            )
            # This is an ambiguous-overload error (see
            # test_cross_module_member_call_is_ambiguous above), but it
            # should never crash LLDB, no matter how many times it is
            # repeated.
            self.expect(
                "expr a_outer->make().secret + b_outer->make().secret", error=True
            )
            self.expect("target modules dump ast --filter Outer")
            self.expect("target dump typesystem")
