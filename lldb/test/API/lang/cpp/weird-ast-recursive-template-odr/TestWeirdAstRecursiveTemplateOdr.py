"""
Test LLDB's behaviour when a self-referential class template specialization
has two conflicting definitions across a main executable and a dylib, and a
linked list of instances of that specialization is split across both
modules.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstRecursiveTemplateOdrTestCase(TestBase):
    def test_main_exe_list_alone(self):
        """
        Walking the linked list that lives entirely inside the main
        executable (using only main.cpp's definition of 'Node<int>') should
        work fine on its own, before the dylib's conflicting definition of
        the same specialization ever gets imported into the scratch AST
        context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_head.val", result_type="int", result_value="2")
        self.expect_expr("main_head.next->val", result_type="int", result_value="3")
        self.expect_expr("main_head.next->next", result_type="Node<int> *")

    @expectedFailureAll(
        bugnumber="self-referential, ODR-conflicting template specializations "
        "(same 'Node<int>' with different bodies in the main executable and a "
        "dylib) confuse LLDB's cross-module linked-list traversal: walking the "
        "chain across both conflicting definitions evaluates without crashing "
        "but returns a wrong value instead of the correct one or a clean "
        "diagnostic"
    )
    def test_walk_chain_across_modules(self):
        """
        Tests LLDB's behaviour when the exact same self-referential template
        specialization ('Node<int>') has two incompatible, self-referential
        definitions: one in the main executable ('Node<T> { Node<T> *next;
        T val; }') and a conflicting one in a dylib that has an extra field
        squeezed in ('Node<T> { Node<T> *next; int tag; T val; }').

        A small linked list of 'Node<int>' objects is split across both
        modules (plugin_head -> main_head -> main_tail), and evaluating an
        expression that walks the whole chain forces LLDB to import and
        reconcile both conflicting definitions of 'Node<int>' within the
        same expression's AST context. Because 'Node<int>' reaches itself
        through the 'next' member, the ASTImporter may end up trying to
        import the (conflicting) pointee type of 'next' - i.e. 'Node<int>'
        itself - again while it is still in the middle of importing/
        reconciling 'Node<int>', which risks unbounded recursion (and a
        stack overflow) instead of a clean ODR/layout diagnostic.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # This alone should not crash LLDB, even though it has to reconcile
        # the dylib's conflicting 'Node<int>' with whatever it already knows
        # about 'Node<int>' from the main executable.
        self.expect_expr("plugin_head.val", result_type="int", result_value="1")

        # Walk from the dylib's node across the module boundary into the
        # main executable's nodes, and back out again. This mixes both
        # conflicting 'Node<int>' definitions in a single expression and
        # recurses through the self-referential 'next' member at every step.
        self.expect_expr(
            "plugin_head.next->val",
            result_type="int",
            result_value="2",
        )
        self.expect_expr(
            "plugin_head.next->next->val",
            result_type="int",
            result_value="3",
        )
        self.expect_expr(
            "plugin_head.next->next->next",
            result_type="Node<int> *",
            result_value="0x0000000000000000",
        )
