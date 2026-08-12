"""
Test LLDB's behaviour when the same-named class 'Token' defines its
'operator==' comparison INLINE, as a 'friend' function inside the class
body, in both the main executable and a dylib -- but with genuinely
different field layouts and different comparison semantics.

An inline friend function is only ever findable via argument-dependent
lookup (ADL) on 'Token': it is never declared at namespace scope, and it
is parsed as part of Token's class body. This means each translation
unit's 'Token' drags in its own uniquely-scoped 'operator==' FunctionDecl
that isn't an ordinary namespace-scope declaration LLDB's
DWARFASTParserClang/ASTImporter machinery can trivially unify across
modules.

'ta' (the main executable's 'Token') and 'tb' (the dylib's 'Token') are
both alive at the same time when the process stops in 'plugin_entry', so
evaluating 'expr ta == ta', 'expr tb == tb', and 'expr ta == tb' in the
same expression-evaluation session forces the per-target shared scratch
AST context to reconcile both modules' conflicting 'Token'
RecordDecls/hidden friend 'operator==' FunctionDecls via the ASTImporter.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFriendOperatorInlineOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Evaluating 'ta == ta' (the main executable's 'Token', using its
        own inline friend 'operator==') and 'tb == tb' (the dylib's
        'Token', using its own, differently-shaped inline friend
        'operator==') each work correctly on their own.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("ta == ta", result_type="bool", result_value="true")
        self.expect_expr("tb == tb", result_type="bool", result_value="true")

    def test_mixed_comparison_does_not_crash(self):
        """
        Tests LLDB's behaviour when comparing the main executable's
        'Token' ('ta') against the dylib's ODR-conflicting 'Token'
        ('tb') directly, via the ADL-only, inline friend 'operator=='.

        Since each module's inline friend 'operator==' is only ever
        findable via ADL for *its own* module's 'Token' RecordDecl, and
        the two RecordDecls end up incompatible once both are pulled
        into the shared scratch AST context, this can plausibly either:
          - fail gracefully (no viable 'operator==' found, or an
            ambiguous-overload error), or
          - crash LLDB outright, e.g. inside Sema::AddOverloadCandidate
            while comparing parameter types that nominally name the
            "same" merged 'Token' type but are actually rooted in two
            different (but same-named) RecordDecls with incompatible
            layouts.

        Either way, this must never crash LLDB.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull the main executable's inline-friend-operator 'Token' into
        # the scratch AST context.
        self.expect_expr("ta == ta", result_type="bool", result_value="true")

        # Now pull the dylib's differently-shaped, ODR-conflicting
        # 'Token' into the same scratch AST context.
        self.expect_expr("tb == tb", result_type="bool", result_value="true")

        # Comparing the two conflicting 'Token' globals directly should
        # never crash LLDB, no matter whether it succeeds, fails
        # gracefully, or reports an ambiguous-overload error.
        self.expect("expr ta == tb")

        # Dumping each module's AST filtered by 'Token' still has to
        # traverse both conflicting 'Token' RecordDecls, each carrying
        # its own hidden, ADL-only friend 'operator==' FunctionDecl.
        # This should never crash LLDB either.
        self.expect("target modules dump ast --filter Token")

        # Dumping the shared scratch typesystem after both conflicting
        # 'Token' definitions have been referenced should also never
        # crash LLDB.
        self.expect("target dump typesystem")

    @expectedFailureAll(
        bugnumber="comparing 'ta == tb' across two ODR-conflicting Token "
        "types whose operator== is an inline, ADL-only friend function "
        "fails to find a viable operator== instead of comparing the "
        "'kind' fields (or reporting an ambiguous-overload error)"
    )
    def test_ta_eq_tb_evaluates_correctly(self):
        """
        Documents a real limitation: comparing 'ta' (main executable's
        'Token', kind=1) against 'tb' (dylib's 'Token', kind=1, tag=0)
        should ideally either report an unambiguous, well-formed error
        (these are, after all, two incompatible ODR-violating types), or
        evaluate using one of the two hidden friend 'operator==' overloads.
        Instead, since neither module's inline friend 'operator==' is
        declared at namespace scope, ADL-based operator lookup for the
        mixed expression finds no viable candidate at all once both
        conflicting 'Token' RecordDecls are visible in the shared scratch
        AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("ta == ta", result_type="bool", result_value="true")
        self.expect_expr("tb == tb", result_type="bool", result_value="true")

        # 'ta.kind' (1) matches 'tb.kind' (1), and 'tb.tag' is 0, so if
        # either operand's own 'operator==' were consistently used, this
        # would evaluate to 'true'.
        self.expect_expr("ta == tb", result_type="bool", result_value="true")

    def test_repeated_ambiguous_expressions_and_dumps_do_not_crash(self):
        """
        Repeatedly evaluates expressions that mix both conflicting
        'Token' definitions -- including via a reinterpret_cast that
        forces comparisons in both operand orders -- interleaved with
        AST/typesystem dumps, to stress-test the
        ASTImporter/DWARFASTParserClang machinery for a crash under
        repeated re-entry rather than a single one-shot evaluation.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        for _ in range(3):
            # These may succeed, fail gracefully, or report an
            # ambiguous-overload/no-viable-candidate error, but they
            # should never crash LLDB, no matter how many times they are
            # repeated or in which order the two conflicting 'Token'
            # globals are compared.
            self.expect("expr ta == tb")
            self.expect("expr tb == ta")
            self.expect("expr *(Token*)&ta == *(Token*)&tb")
            self.expect("expr *(Token*)&tb == *(Token*)&ta")
            self.expect("target modules dump ast --filter Token")
            self.expect("target dump typesystem")
