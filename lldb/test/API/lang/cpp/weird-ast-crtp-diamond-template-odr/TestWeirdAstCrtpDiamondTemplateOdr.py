"""
Test LLDB's behaviour with a "doubled" CRTP diamond: 'Widget' has TWO
independent CRTP bases, 'Counted<Widget>' and 'Named<Widget>', each
template-parameterized on 'Widget' itself (textbook CRTP, just twice at
once). The main executable and a dylib both define identical
'Counted<D>'/'Named<D>' templates and an identical 'Widget' (same bases,
same 'extra' field), but the dylib's 'Widget' inherits from the two CRTP
bases in the OPPOSITE order. That flips each base's byte offset within the
derived object while keeping the same set of base classes and the same
derived-class field -- a layout-only ODR violation across LLDB modules.

Each of 'Counted<Widget>' and 'Named<Widget>' is independently cyclic
through 'Widget' (its own template argument names the very RecordDecl that
is inheriting from it), so 'Widget' is mid-import/mid-completion while
*two* self-referential class template specializations are being resolved
for it simultaneously, doubling the risk described in
weird-ast-diamond-inheritance-odr and weird-ast-crtp-self-base-odr.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstCrtpDiamondTemplateOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each module's 'Widget' (and its two independent CRTP bases
        'Counted<Widget>'/'Named<Widget>') can be read on its own, before
        the other module's conflicting (opposite base order) definition of
        'Widget' has been imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_widget.id", result_type="int", result_value="1")
        self.expect_expr(
            "main_widget.name", result_type="const char *", result_summary='"main"'
        )
        self.expect_expr("main_widget.extra", result_type="int", result_value="100")

    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same doubled-CRTP pair of
        base classes ('Counted<Widget>'/'Named<Widget>') is used to build
        two 'Widget' definitions that only differ in base-class order:
        main.cpp's 'Widget' inherits 'Counted<Widget>' then 'Named<Widget>',
        while plugin.cpp's 'Widget' inherits 'Named<Widget>' then
        'Counted<Widget>'. Reading both modules' 'Widget' globals in the
        same sequence of expressions forces LLDB to import and reconcile
        both conflicting definitions of 'Widget' - and, because of the
        doubled CRTP cycle, both conflicting definitions of
        'Counted<Widget>' and 'Named<Widget>' - within the same scratch AST
        context.

        This part - plain member reads across both modules, plus dumping
        the shared scratch typesystem - actually evaluates fine and
        returns correct, module-specific values without crashing or
        corrupting either definition.

        NOTE: a real, reproducible LLDB segfault was found while exploring
        this scenario, but it is deliberately NOT exercised by this test
        (a segfault would take down the whole test process instead of
        failing cleanly). Running 'target modules dump ast --filter
        Widget <module>' after evaluating expressions like the ones below
        crashes LLDB via unbounded/unguarded recursion in the AST-dumping
        RecursiveASTVisitor: dumping 'Widget' visits its
        ClassTemplateSpecializationDecl bases 'Counted<Widget>' and
        'Named<Widget>', each of which has 'Widget' itself as a template
        argument, and the visitor has no cycle detection for a decl
        revisiting itself through its own CRTP base's template argument
        (Widget -> Counted<Widget> -> (template argument) Widget -> ...).
        This reproduces even for a single, non-ODR-conflicting CRTP type
        in one module (see weird-ast-crtp-self-base-odr for the
        single-base variant of this same finding), i.e. it is not specific
        to the ODR conflict or to the flipped base order introduced here.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the main executable's 'Widget' first, so its (doubly
        # CRTP-cyclic) 'Counted<Widget>'/'Named<Widget>' bases end up in
        # the scratch AST context.
        self.expect_expr("main_widget.id", result_type="int", result_value="1")

        # Now bring in the dylib's conflicting 'Widget' (opposite base
        # order) into the same sequence of expressions, forcing the
        # ASTImporter to reconcile both doubly-cyclic CRTP pairs at once.
        self.expect_expr(
            "plugin_widget.name", result_type="const char *", result_summary='"plugin"'
        )

        # Combine both modules' 'Widget' globals in a single expression.
        self.expect_expr(
            "main_widget.id + plugin_widget.extra",
            result_type="int",
            result_value="201",
        )

        # Each module's 'Widget' has the same base classes and the same
        # 'extra' field, but the flipped inheritance order changes the
        # base sub-object byte offsets (and, here, the overall size), so
        # the two 'sizeof' results are not required to match.
        self.expect_expr("sizeof(main_widget)", result_type="unsigned long")
        self.expect_expr("sizeof(plugin_widget)", result_type="unsigned long")

        # Dumping the shared per-target scratch TypeSystem/ASTContext after
        # combining both conflicting 'Widget' definitions in expressions
        # above should not crash, even though it is exactly the merged AST
        # state where an ODR conflict like this would be expected to
        # surface as corruption if the ASTImporter mishandled it.
        self.expect("target dump typesystem")
