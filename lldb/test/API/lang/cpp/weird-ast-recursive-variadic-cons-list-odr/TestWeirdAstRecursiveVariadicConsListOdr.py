"""
Test LLDB's behaviour when the *base case* of a recursively-instantiated
variadic class template has two conflicting definitions across a main
executable and a dylib, while the recursive case that depends on it stays
textually identical in both modules.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstRecursiveVariadicConsListOdrTestCase(TestBase):
    def test_each_list_alone(self):
        """
        Walking each module's own 'Cons<int,int,int>' list on its own
        (never mixing the main executable's and the dylib's conflicting
        base-case 'Cons<int>' in the same expression) should work fine.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_list.head", result_type="int", result_value="1")
        self.expect_expr(
            "main_list.rest->head", result_type="int", result_value="2"
        )
        self.expect_expr(
            "main_list.rest->rest->head", result_type="int", result_value="3"
        )

        self.expect_expr("plugin_list.head", result_type="int", result_value="10")
        self.expect_expr(
            "plugin_list.rest->head", result_type="int", result_value="20"
        )
        self.expect_expr(
            "plugin_list.rest->rest->head", result_type="int", result_value="30"
        )
        self.expect_expr(
            "plugin_list.rest->rest->sentinel",
            result_type="int",
            result_value="2989",
        )

    @expectedFailureAll(
        bugnumber="an ODR conflict confined to the *base case* of a chain of "
        "recursively-dependent variadic template instantiations (here, "
        "'Cons<int>' - reached only after completing 'Cons<int,int,int>' and "
        "'Cons<int,int>', which are textually identical in both modules) "
        "corrupts LLDB's scratch AST context: once both conflicting "
        "definitions of the base case have been imported, an expression that "
        "references one of the two top-level lists ('main_list' or "
        "'plugin_list') after already having referenced the other one in the "
        "same expression spuriously fails with 'use of undeclared "
        "identifier', instead of evaluating (correctly or with a clean ODR "
        "diagnostic)"
    )
    def test_walk_chain_across_modules(self):
        """
        Tests LLDB's behaviour when the base case of a variadic
        Lisp-style cons list template ('Cons<Head>', the explicit
        specialization that terminates the recursive case 'Cons<Head,
        Tail...>') has two incompatible definitions: main.cpp's
        'Cons<Head> { Head head; void *rest; }' and a conflicting one in
        the dylib that has an extra field squeezed in ('Cons<Head> { Head
        head; int sentinel; void *rest; }'). The recursive case itself
        ('Cons<Head, Tail...> { Head head; Cons<Tail...> *rest; }') is
        byte-for-byte identical in both modules.

        'Cons<int,int,int>' is instantiated separately in both the main
        executable (main_list) and the dylib (plugin_list); each
        recursively instantiates 'Cons<int,int>' and, at the bottom, the
        conflicting base case 'Cons<int>'. The dylib's innermost
        'Cons<int>' node's 'rest' pointer is wired across the module
        boundary onto the main executable's top-level list, so a single
        expression evaluated while stopped in the dylib can reference both
        modules' top-level lists together, forcing LLDB to import and
        reconcile both conflicting definitions of the base case
        'Cons<int>' within the same expression's AST context. Because the
        conflict is only reached after walking down two levels of
        (identical) recursive instantiations, any recursion/ODR-conflict
        bookkeeping tailored for a *top-level* self-referential conflict
        may not correctly cover this nested-base-case scenario.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Referencing the dylib's list alone should not crash LLDB, even
        # though it has to import the dylib's conflicting base-case
        # 'Cons<int>' into the scratch AST context.
        self.expect_expr("plugin_list.head", result_type="int", result_value="10")

        # Referencing the main executable's list alone (after the dylib's
        # conflicting base case has already been imported) should also
        # work.
        self.expect_expr("main_list.head", result_type="int", result_value="1")

        # Combine both conflicting base-case specializations of
        # 'Cons<int>' in a single expression. This is the crux of the
        # test: it should evaluate to 33 (30 from the dylib's list plus 3
        # from the main executable's list), but instead spuriously fails
        # to find whichever of 'plugin_list'/'main_list' is referenced
        # second, because the scratch AST context ends up with two
        # different (and disagreeing) decls for the base case
        # 'Cons<int>'.
        self.expect_expr(
            "plugin_list.rest->rest->head + main_list.rest->rest->head",
            result_type="int",
            result_value="33",
        )
