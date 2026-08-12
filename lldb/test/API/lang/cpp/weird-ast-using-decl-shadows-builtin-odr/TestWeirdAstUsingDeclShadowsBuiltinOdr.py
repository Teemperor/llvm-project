"""
Test LLDB's expression evaluator against an ODR violation on a *using-decl'd*
overloaded operator: two modules each define an incompatible 'mynum::BigInt'
(different field layouts) together with a matching 'mynum::operator+'
overload for their own 'BigInt' -- and, in both modules, bring the operator
and the struct into the *global* namespace via the exact same
using-declarations ('using mynum::operator+;' / 'using mynum::BigInt;').

Unlike the sibling 'weird-ast-operator-overload-odr' test (which only uses
ordinary qualified names), a using-declaration naming an overloaded operator
goes through a dedicated Sema path (UsingShadowDecl / redeclaration checking
for using-declared operators) that is normally never asked to reconcile two
different, ABI-incompatible 'operator+' overloads (one returning 'BigInt' by
value, the other returning 'double' in a scalar register) for the exact same
parameter types, once both get imported into the shared per-target scratch
AST context.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstUsingDeclShadowsBuiltinOdrTestCase(TestBase):
    def setup_test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

    def test_each_alone(self):
        """
        Each module's using-declared 'BigInt'/'operator+' pair works fine
        via the unqualified, using-declaration-injected name when used on
        its own.
        """
        self.setup_test()

        self.expect_expr("xa + xa", result_type="mynum::BigInt")
        self.expect_expr("xb + xb", result_type="double", result_value="10")

    def test_mixed_expression(self):
        """
        Evaluates an expression that mixes 'xa' (main executable's
        'BigInt') and 'xb' (dylib's incompatible 'BigInt') in the same
        call to the using-declaration-injected 'operator+'. Overload
        resolution should reject this: LLDB keeps the two conflicting
        'BigInt' RecordDecls distinct in the scratch AST context rather
        than silently merging them, so neither module's 'operator+'
        accepts the other module's 'BigInt'. This should never crash
        LLDB, even though the two 'operator+' overloads that end up
        shadowed by the (textually identical) using-declarations return
        ABI-incompatible types for identical parameter types.
        """
        self.setup_test()

        self.expect(
            "expr xa + xb",
            error=True,
            substrs=["invalid operands to binary expression"],
        )

    @expectedFailureAll(
        bugnumber="The very first expression evaluated at a breakpoint "
        "that calls the *qualified* name 'mynum::operator+(xa, xa)' is "
        "reported as ambiguous, even though only one module's 'BigInt' is "
        "involved: qualified name lookup for 'mynum::operator+' pulls in "
        "both modules' incompatible 'operator+' overloads (one returning "
        "'BigInt', the other 'double', for identical parameter types) from "
        "debug info simultaneously on that first lookup, and neither is "
        "preferred, unlike the unqualified using-declaration-injected call "
        "('xa + xa'), which always resolves correctly regardless of "
        "ordering"
    )
    def test_qualified_call_ambiguous_on_first_use(self):
        """
        As the very first expression evaluated at the breakpoint, calls
        the *qualified* name 'mynum::operator+' with only one module's own
        operands ('xa', 'xa'). This should unambiguously resolve to that
        module's 'operator+', but instead reports an ambiguity between the
        two ODR-violating overloads, because looking up the qualified name
        imports both modules' 'operator+' decls into the scratch AST
        context's merged 'mynum' namespace at once. Once the ambiguity has
        been triggered once, subsequent identical calls resolve fine (see
        the passing assertion below), which is itself a symptom of the
        underlying scratch-AST-context corruption.
        """
        self.setup_test()

        self.expect_expr(
            "mynum::operator+(xa, xa)", result_type="mynum::BigInt"
        )

    def test_qualified_call_after_both_imported(self):
        """
        Same qualified call as above, but evaluated only after both
        conflicting 'operator+' overloads have already been imported into
        the scratch AST context via the unqualified, using-declaration-
        injected names. In this ordering the qualified call resolves
        correctly and should never crash LLDB.
        """
        self.setup_test()

        self.expect_expr("xa + xa", result_type="mynum::BigInt")
        self.expect_expr("xb + xb", result_type="double", result_value="10")

        self.expect_expr(
            "mynum::operator+(xa, xa)", result_type="mynum::BigInt"
        )

    def test_dump_typesystem_after_mixed_expression(self):
        """
        After expressions have forced both conflicting 'BigInt' decls (and
        both using-declaration-shadowed 'operator+' overloads) into the
        scratch AST context, dumping the scratch type system should not
        crash LLDB, and should show both (structurally different)
        'BigInt' RecordDecls rather than a single corrupted/merged one.
        """
        self.setup_test()

        self.expect_expr("xa + xa", result_type="mynum::BigInt")
        self.expect_expr("xb + xb", result_type="double", result_value="10")
        self.expect(
            "expr xa + xb",
            error=True,
            substrs=["invalid operands to binary expression"],
        )

        self.expect("target dump typesystem", substrs=["BigInt"])
