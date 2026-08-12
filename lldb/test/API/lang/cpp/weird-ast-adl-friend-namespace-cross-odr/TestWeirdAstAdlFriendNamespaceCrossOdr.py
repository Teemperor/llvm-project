"""
Test LLDB's handling of an argument-dependent-lookup (ADL) call to an
overloaded free function ('geo::translate') whose overloads differ in
*arity* (one 'dx' offset vs. 'dx'+'dy') across an ODR violation in the
call's associated namespace, where the ODR violation also affects the
argument type itself: the main executable's 'geo::Point' has two 'int'
members ('x', 'y'), while the dylib's 'geo::Point' has three ('x', 'y',
'z'). The two 'translate' overloads and the two 'Point' definitions are
found only via ADL (there is no shared header or using-declaration
pulling them together).

Both 'pa' (the main executable's 2-field 'geo::Point') and 'pb' (the
dylib's 3-field 'geo::Point') are alive at the same time when the
process stops in 'plugin_entry'. Evaluating 'expr translate(pa, 5)'
(which should ADL-resolve to main.cpp's 1-offset overload) followed by
'expr translate(pb, 5, 6)' (which should ADL-resolve to plugin.cpp's
2-offset overload) in the same expression-evaluation session forces the
per-target shared scratch AST context to import/reconcile both
conflicting 'geo' namespaces -- including both incompatible 'Point'
definitions and both differently-shaped 'translate' overloads -- via the
ASTImporter.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstAdlFriendNamespaceCrossOdrTestCase(TestBase):
    def test_vb_alone(self):
        """
        Evaluating the unqualified, ADL-driven call 'translate(pb, 5, 6)'
        by itself (the first and only expression evaluated in this debug
        session), right after stopping in 'plugin_entry', works
        correctly: 'pb's associated namespace ('geo', found via the
        dylib's DWARF) has only the dylib's own 2-offset 'translate'
        overload visible at this point, so the call unambiguously
        resolves to it and evaluates 'pb.x + 5, pb.y + 6, pb.z'
        untouched.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "translate(pb, 5, 6)",
            result_type="geo::Point",
            result_children=[
                ValueCheck(name="x", value="5"),
                ValueCheck(name="y", value="6"),
                ValueCheck(name="z", value="0"),
            ],
        )

    @expectedFailureAll(
        bugnumber="Evaluating an unqualified, ADL-driven call to a free "
        "function ('geo::translate') whose argument's associated "
        "namespace ('geo') is genuinely ODR-violating across two modules "
        "(both the free function's arity and the argument type's field "
        "count/layout differ) silently produces a wrong-but-well-formed "
        "result: 'translate(pa, 5)' evaluated as the very first "
        "expression in a fresh debug session -- with only the main "
        "executable's 2-field 'geo::Point'/1-offset 'translate' ever "
        "referenced -- returns 'x = 0, y = 0' instead of the correct "
        "'x = 5, y = 0', because the current frame's lexical context is "
        "plugin.cpp (where 'plugin_entry' is defined), and the scratch "
        "AST context resolves the ADL call against plugin.cpp's "
        "lazily-deserialized, still-incomplete 'geo::Point'/'translate' "
        "instead of 'pa's own DWARF-derived 2-field type"
    )
    def test_va_alone(self):
        """
        Evaluating 'translate(pa, 5)' as the first and only expression in
        a fresh debug session, using only main.cpp's 2-field 'geo::Point'
        and 1-offset 'translate' overload, should return 'x = 5, y = 0'
        (i.e. 'pa.x + 5, pa.y'). Instead it silently returns
        'x = 0, y = 0': because the stopped frame ('plugin_entry') lives
        in plugin.cpp, the scratch AST context ends up resolving the
        call against plugin.cpp's conflicting 'geo' namespace instead of
        'pa's own module.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "translate(pa, 5)",
            result_type="geo::Point",
            result_children=[
                ValueCheck(name="x", value="5"),
                ValueCheck(name="y", value="0"),
            ],
        )

    def test_both_together(self):
        """
        Tests LLDB's behaviour when both conflicting 'geo::Point'/
        'geo::translate' definitions are used together in the same
        expression-evaluation session: evaluating the ADL-driven
        'translate(pa, 5)' immediately followed by 'translate(pb, 5, 6)'
        (pulling both modules' conflicting 'geo' namespaces into the
        shared per-target scratch AST context together), and then
        attempting to call the *wrong* module's 2-offset overload on
        'pa' ('translate(pa, 5, 6)', which is not a call any translation
        unit could ever have made -- main.cpp's 'translate' only takes
        one offset).

        None of this should crash LLDB. The first two calls'
        (wrong-but-well-formed) results are documented separately by
        'test_va_alone'/'test_vb_alone' above; this test only asserts
        that the whole sequence doesn't crash and that the deliberately
        invalid call to the dylib's 2-offset overload with a mismatched
        1-offset argument is rejected as a compile-time overload
        resolution failure rather than being silently accepted (which
        would indicate the two 'Point' definitions got merged into a
        single, ambiguous type in the scratch AST context).
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect("expr translate(pa, 5)")
        self.expect("expr translate(pb, 5, 6)")

        # 'pa' only ever appears with main.cpp's one-offset 'translate'.
        # Calling the dylib's two-offset overload on it is not a call any
        # translation unit could make, and should be rejected outright
        # rather than silently "succeeding" against a merged/ambiguous
        # 'geo::Point'.
        self.expect(
            "expr translate(pa, 5, 6)",
            error=True,
            substrs=["no matching function for call to 'translate'"],
        )
