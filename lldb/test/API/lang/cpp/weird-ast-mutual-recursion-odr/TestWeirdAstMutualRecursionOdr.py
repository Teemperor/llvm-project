import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstMutualRecursionOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Tests LLDB's behaviour when two mutually-recursive structs ('A'
        and 'B', where 'A' has a 'B*' member and 'B' has an 'A*' member)
        are defined in both the main executable and a dylib, but only one
        half of the cycle ('B') has an ODR conflict between the two
        modules: the dylib's 'B' has an extra 'int extra' data member
        that the exe's 'B' does not have, while 'A' is byte-for-byte
        identical in both modules.

        Because 'A' and 'B' reference each other, importing either type
        from DWARF into LLDB's scratch AST context transitively pulls in
        the other: the ASTImporter has to deal with 'A' and 'B' being
        simultaneously "in progress" (incomplete) while it resolves the
        cycle.

        This test walks each module's 'A'/'B' object graph on its own,
        without ever forcing both modules' conflicting notions of 'B'
        into the scratch AST context at the same time, and should
        therefore succeed.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the exe's first 'A'/'B' pair first, so their
        # definitions end up in LLDB's scratch AST context.
        self.expect_expr("g_exe_a.x", result_type="int", result_value="1")
        self.expect_expr("g_exe_b.y", result_type="int", result_value="2")

        # Walk the purely-exe-side cycle: g_exe_a.b->a->b->y should get
        # back to g_exe_b.y.
        self.expect_expr("g_exe_a.b->a->b->y", result_type="int", result_value="2")

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile two mutually-recursive, "
        "ODR-conflicting struct definitions ('A' pointing to 'B' and 'B' "
        "pointing back to 'A', where only 'B' differs between modules): "
        "once one module's 'B' has been imported into the scratch AST "
        "context, looking up a member that only exists on the *other* "
        "module's 'B' fails with 'no member named ... in B' instead of "
        "resolving to the right definition"
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when two mutually-recursive structs ('A'
        and 'B', where 'A' has a 'B*' member and 'B' has an 'A*' member)
        are defined in both the main executable and a dylib, but only one
        half of the cycle ('B') has an ODR conflict between the two
        modules: the dylib's 'B' has an extra 'int extra' data member
        that the exe's 'B' does not have, while 'A' is byte-for-byte
        identical in both modules.

        Because 'A' and 'B' reference each other, importing either type
        from DWARF into LLDB's scratch AST context transitively pulls in
        the other: the ASTImporter has to deal with 'A' and 'B' being
        simultaneously "in progress" (incomplete) while it resolves the
        cycle. Tainting only one side of that cycle with an ODR conflict
        is meant to stress the bookkeeping that tracks partially-imported
        decls for both types at once - e.g. if a decl for 'B' (or 'A') is
        cached before its conflicting status relative to the other
        module's 'B' is fully known, subsequent lookups/imports could see
        inconsistent state and crash rather than merely returning a wrong
        answer.

        The test builds a small graph of 'A'/'B' objects split across the
        exe and the dylib, cross-links some of them so pointers cross the
        module boundary in both directions, and then evaluates
        expressions that walk the cycle (a->b->a->b->... etc), forcing
        LLDB to reconcile both modules' conflicting notions of 'B' (and
        the 'A' that depends on it) within a single expression.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the exe's first 'A'/'B' pair first, so their
        # definitions end up in LLDB's scratch AST context.
        self.expect_expr("g_exe_a.x", result_type="int", result_value="1")
        self.expect_expr("g_exe_b.y", result_type="int", result_value="2")

        # Walk the purely-exe-side cycle: g_exe_a.b->a->b->y should get
        # back to g_exe_b.y.
        self.expect_expr("g_exe_a.b->a->b->y", result_type="int", result_value="2")

        # Now bring in the dylib's conflicting 'B' (and its dependent
        # 'A') from the same expression. This forces the ASTImporter to
        # reconcile the two conflicting CXXRecordDecls for 'B' (and,
        # transitively, the two decls for 'A') while both are alive in
        # the same AST context.
        self.expect_expr("g_plugin_b->extra", result_type="int", result_value="99")
        self.expect_expr("g_plugin_a->x", result_type="int", result_value="10")

        # Walk from the dylib's 'A' into its conflicting 'B' and back out
        # to the exe's second 'A' ('g_exe_a2'), which main() cross-linked
        # in after plugin_init() ran. This single expression therefore
        # has to resolve 'A'/'B' decls that originate from *both*
        # modules, with the ODR-conflicting 'B' in the middle of the
        # chain.
        self.expect_expr("g_plugin_a->b->a->x", result_type="int", result_value="3")

        # Continue the walk one more hop: from the exe's 'g_exe_a2' back
        # into the dylib's conflicting 'B', reading the extra field that
        # only exists in the dylib's definition.
        self.expect_expr(
            "g_plugin_a->b->a->b->extra", result_type="int", result_value="99"
        )

        # Same cycle, started from the exe side: g_exe_a2.b is the
        # dylib's (conflicting) 'B', whose 'a' pointer was rewired back
        # to point at g_exe_a2 itself.
        self.expect_expr("g_exe_a2.b->extra", result_type="int", result_value="99")
        self.expect_expr("g_exe_a2.b->a->x", result_type="int", result_value="3")

        # g_exe_b2.a points at the dylib's 'g_plugin_a', whose 'b' member
        # points at the dylib's conflicting 'B'. Walk that chain too.
        self.expect_expr("g_exe_b2.a->x", result_type="int", result_value="10")
        self.expect_expr("g_exe_b2.a->b->extra", result_type="int", result_value="99")

        # Finally, print whole objects (forces complete record layout for
        # the conflicting 'B' definitions) and take sizes across both
        # modules in a single expression.
        self.expect("expression g_exe_b")
        self.expect("expression *g_plugin_b")
        self.expect("expression sizeof(g_exe_b) + sizeof(*g_plugin_b)")
