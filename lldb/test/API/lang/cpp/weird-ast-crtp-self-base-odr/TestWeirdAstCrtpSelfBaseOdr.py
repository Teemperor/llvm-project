"""
Test LLDB's behaviour with a classic CRTP (curiously recurring template
pattern) pair - 'Base<Derived>'/'Derived' - that has genuinely conflicting
definitions of 'Base<Derived>' across a main executable and a dylib, while
'Derived' in both modules still names 'Base<Derived>' as its base.

CRTP makes 'Base<Derived>' and 'Derived' mutually dependent in the AST:
'Derived's base-specifier names a specialization of 'Base' whose template
argument is 'Derived' itself, i.e. the class template specialization is
directly cyclic (it appears as its own base's template argument). This
test exercises what happens when the ASTImporter has to import and
reconcile that cyclic pair as the base-class subobject of a cross-module
'Derived', with the added complication that the two modules' 'Base<Derived>'
have different layouts (swapped/extra members).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstCrtpSelfBaseOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each module's 'Derived' (and its CRTP base 'Base<Derived>') can be
        read on its own, before the other module's conflicting definition
        of 'Base<Derived>' has been imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "derived_from_main.common", result_type="int", result_value="1"
        )
        self.expect_expr("derived_from_main.extra", result_type="int", result_value="2")

    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same CRTP pair
        ('Base<Derived>'/'Derived') has two incompatible definitions of
        'Base<Derived>': one in the main executable ('Base<D> { D *self();
        int common; }') and a conflicting one in a dylib that swaps the
        field order and adds an extra field ('Base<D> { D *self(); int
        extra_in_base; int common; }'). 'Derived' itself ('struct Derived :
        Base<Derived> { int extra; }') is byte-for-byte identical in both
        modules, but its base class is not.

        Reading both modules' 'Derived' objects (and their 'Base<Derived>'
        base-class subobjects) in the same sequence of expressions forces
        LLDB to import and reconcile both conflicting definitions of
        'Base<Derived>' - and, because of the CRTP cycle, transitively
        'Derived' itself - within the same scratch AST context.

        This part - plain member reads and whole-object prints - actually
        evaluates fine and returns the correct, module-specific layouts
        without crashing or corrupting either definition: LLDB keeps the
        two modules' 'Derived'/'Base<Derived>' decls distinct rather than
        incorrectly unifying them. The real crash found while exploring
        this scenario is one step further and is NOT exercised by this
        test (see the module docstring above): running 'target modules
        dump ast --filter Derived' after evaluating expressions like these
        segfaults LLDB outright via unbounded recursion in the AST-dumping
        RecursiveASTVisitor, which has no cycle detection for a decl
        revisiting itself through its own CRTP base's template argument
        (Derived -> Base<Derived> -> (template argument) Derived -> ...).
        That reproduces even for a single, non-ODR-conflicting CRTP type
        in one module; deliberately not exercised here since a real
        segfault would take down the whole test process instead of failing
        cleanly.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the main executable's 'Derived' first, so its
        # (CRTP-cyclic) 'Base<Derived>' ends up in the scratch AST context.
        self.expect_expr(
            "derived_from_main.common", result_type="int", result_value="1"
        )

        # Now bring in the dylib's conflicting 'Derived'/'Base<Derived>'
        # (different field order/extra field in the base) into the same
        # expression, forcing the ASTImporter to reconcile both cyclic
        # CRTP pairs at once.
        self.expect_expr(
            "derived_from_plugin->common", result_type="int", result_value="3"
        )
        self.expect_expr(
            "derived_from_plugin->extra_in_base", result_type="int", result_value="4"
        )
        self.expect_expr(
            "derived_from_plugin->extra", result_type="int", result_value="5"
        )

        # Combine both modules' conflicting 'Base<Derived>' base-class
        # members in a single expression.
        self.expect_expr(
            "derived_from_plugin->common + derived_from_main.common",
            result_type="int",
            result_value="4",
        )

        # Finally, print whole objects (forces complete record layout,
        # including the base-class subobject, for both conflicting
        # 'Base<Derived>' definitions) in the same expression.
        self.expect("expression derived_from_main")
        self.expect("expression *derived_from_plugin")
        self.expect_expr(
            "sizeof(derived_from_main) + sizeof(*derived_from_plugin)",
            result_value="20",
        )
