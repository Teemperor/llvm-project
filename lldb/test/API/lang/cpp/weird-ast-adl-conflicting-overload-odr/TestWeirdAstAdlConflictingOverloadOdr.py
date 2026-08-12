"""
Test LLDB's behaviour when evaluating an unqualified call that relies on
argument-dependent lookup (ADL) to find a free function 'ns::len' via its
argument's associated namespace ('ns::Vec's enclosing namespace 'ns'),
where the main executable and a dylib each define a *conflicting*
'ns::Vec' (different layout: one 'int' member vs two) and a conflicting
'ns::len' overload for it (different return type: 'int' vs 'double') --
a genuine ODR violation across the two modules for the exact same
qualified name and parameter type.

Both 'va' (the main executable's 'ns::Vec') and 'vb' (the dylib's
'ns::Vec') are alive at the same time when the process stops in
'plugin_entry', so evaluating 'expr len(va)' immediately followed by
'expr len(vb)' in the very same expression-evaluation session forces the
per-target shared scratch AST context to reconcile both modules'
conflicting 'ns' namespaces/'len' overloads via the ASTImporter.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstAdlConflictingOverloadOdrTestCase(TestBase):
    def test_va_alone(self):
        """
        Evaluating the unqualified, ADL-driven call 'len(va)' by itself
        (the *first and only* expression evaluated in this debug
        session), right after stopping in 'plugin_entry', does not crash
        LLDB. It also does not evaluate correctly: because the process
        has already run plugin.cpp's file-scope code (which contains its
        own unqualified call to 'ns::len'), plugin.cpp's 'ns::len'
        overload is already visible from the current frame's lexical
        context, and looking up 'va's associated namespace 'ns' pulls in
        main.cpp's conflicting 'ns::len' overload too -- so *even calling
        'len' on just one of the two conflicting 'Vec's* ends up with two
        equally-viable, non-template 'ns::len' candidates (they only
        differ in return type, which does not participate in overload
        resolution) and Sema reports the call as ambiguous instead of
        picking either one.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect(
            "expr len(va)",
            error=True,
            substrs=["call to 'len' is ambiguous"],
        )

    def test_vb_alone(self):
        """
        Same as 'test_va_alone' above, but for 'len(vb)' evaluated as the
        first and only expression in a fresh debug session: it is
        likewise (correctly) rejected as ambiguous, for the same reason
        (both conflicting 'ns::len' overloads are visible at once: one
        via the current frame's lexical context, the other via 'vb's
        associated namespace).
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect(
            "expr len(vb)",
            error=True,
            substrs=["call to 'len' is ambiguous"],
        )

    @expectedFailureAll(
        bugnumber="Evaluating an unqualified, ADL-driven call to an "
        "overloaded free function ('ns::len') whose two overloads only "
        "differ by return type ('int' vs 'double') across an ODR "
        "violation in the argument's associated namespace picks "
        "whichever overload happens to be visible from the current "
        "frame's lexical DeclContext, ignoring the argument's own "
        "module: calling 'len(vb)' right after the (correctly-rejected, "
        "ambiguous) 'len(va)' silently succeeds and returns the *other* "
        "module's 'ns::Vec.x + ns::Vec.y' result instead of failing or "
        "computing 'vb.x + vb.y'"
    )
    def test_both_together(self):
        """
        Documents that combining both conflicting 'ns::Vec'/'ns::len'
        definitions in the same expression-evaluation session does not
        crash LLDB, but also does not reliably evaluate 'len(vb)'
        correctly: right after 'expr len(va)' has (correctly) been
        rejected as ambiguous, 'expr len(vb)' unexpectedly *succeeds*,
        because by that point the per-target scratch AST context has
        settled on a single, merged 'ns' namespace whose overload set
        for 'len' silently resolves to the dylib's 'double len(ns::Vec)'
        (its own frame's lexical context) rather than continuing to
        report ambiguity.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect(
            "expr len(va)",
            error=True,
            substrs=["call to 'len' is ambiguous"],
        )

        # This should either also be rejected as ambiguous (like 'len(va)'
        # just above) or evaluate 'vb.x + vb.y' == 11. Instead it silently
        # "succeeds" via the merged scratch AST's overload set.
        self.expect_expr("len(vb)", result_type="double", result_value="11")
