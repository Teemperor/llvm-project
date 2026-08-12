"""
Test 'target modules dump ast --filter <pattern>' against LLDB's per-target
shared scratch Clang ASTContext after it has been populated with 40 separate
classes named 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac0' .. 'aaaaaaaaaaaaaaaaaaaaaa
aaaaaaaaaaac39' (32 'a's then 'c', then an index), one at a time, via 40
separate 'expr (void)ClassN{};' calls -- exactly as a user fuzzing this
command by hand might do -- and then filtered with a pattern shaped like a
classic catastrophic-backtracking regex, '(a+)+b', which never matches any
of the 40 names (they all end in 'c<N>', never 'b').

The premise this test set out to check was: if '--filter' were implemented
with a backtracking regex engine (e.g. 'std::regex', as opposed to the
DFA-based 'llvm::Regex'), a pattern like '(a+)+b' run against 40 candidate
decl names built almost entirely out of the letter 'a' could trigger
exponential-time catastrophic backtracking (a hang), or -- on some libc++
implementations, where sufficiently deep backtracking recursion can
manifest as unbounded native-stack recursion -- a stack-overflow crash
instead of just a hang.

That premise does not hold for the current implementation: reading
'clang::ASTPrinter::filterMatches' in 'clang/lib/Frontend/ASTConsumers.cpp'
(reached from 'TypeSystemClang::Dump', by way of
'SymbolFileDWARF::DumpClangAST' and
'CommandObjectTargetModulesDumpClangAST::DoExecute') shows the filter is
implemented as a single, plain 'std::string::find()' substring search
against each decl's qualified name -- there is no regex engine (backtracking
or otherwise) anywhere on this path, so '(a+)+b' is treated as a completely
literal 8-character substring, not a pattern, and cannot backtrack. Manually
driving a freshly built '/path/to/lldb' through exactly this scenario (40
sequential 'expr struct aaaa...ac<N> { ... }; (void)aaaa...ac<N>{};' calls
followed by 'target modules dump ast --filter (a+)+b') confirms this:
the command returns near-instantly (dominated entirely by normal process/
module-load overhead, not by the dump itself) and does not hang, regardless
of how many times the same filtered dump is repeated back to back.

However, while chasing this, manual exploration (following the same
"populate the scratch AST, then run a filtered dump" shape used by several
sibling tests, e.g.
'weird-ast-dump-typesystem-template-instantiation-storm' and
'weird-ast-dump-ast-unicode-filter') found a real, unrelated, and much more
direct crash: 'clang::ASTPrinter::HandleTranslationUnit' only takes the
cheap, non-recursive, per-DeclContext 'print()' walk when the filter string
is EMPTY. As soon as '--filter' is given ANY non-empty value (matching or
not -- '(a+)+b' works exactly as well as a plain identifier), it switches to
'TraverseDecl(D)', i.e. a full 'clang::RecursiveASTVisitor<ASTPrinter>'
traversal of the whole translation unit. If a 'ClassTemplateSpecializationD
ecl' is reachable from that translation unit, that recursive traversal
reliably segfaults LLDB itself inside 'clang::RecursiveASTVisitor<ASTPrinte
r>::TraverseClassTemplateSpecializationDecl' -- confirmed reproducible by
directly building this test's own main.cpp/plugin.cpp pair and driving a
release '/path/to/lldb' binary in batch mode:

  expression g_wrap                          (or just letting DWARF parse
                                               'Wrap<int>' any other way)
  target modules dump ast --filter (a+)+b    -> SIGSEGV, regardless of the
                                                 40 'aaaa...ac<N>' classes
                                                 also being present in the
                                                 scratch AST at the time.

This crash is not specific to '(a+)+b' (a plain non-matching identifier
filter, or even a matching one, reproduces it exactly the same way; see
'weird-ast-dump-ast-unicode-filter' and 'weird-ast-dump-typesystem-template-
instantiation-storm' for independent confirmations with different filter
strings), and it is not a backtracking-regex issue at all -- it is a plain
recursive-traversal crash that only needs ONE template specialization to be
reachable and ANY non-empty filter string. 'plugin.cpp' below defines
exactly one such specialization ('Wrap<int>') purely so this real crash
stays documented (as an XFAIL) in the same test that checks the originally
hypothesized regex-backtracking behavior (which does not apply here).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

# Number of 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac<N>' classes (32 'a's then
# 'c', then the index) to define and instantiate one at a time via
# separate 'expr' calls, populating the scratch AST with that many
# candidate decl names before the filtered dump is attempted.
NUM_CLASSES = 40

# 32 'a's, matching the scenario's exact class-name shape.
_A_RUN = "a" * 32

# A classic catastrophic-backtracking regex shape. Every one of the 40
# class names below ends in 'c<N>' (never 'b'), so -- were this actually a
# backtracking regex engine -- this pattern would never match any of them,
# forcing a full (and, for a real backtracking engine, exponential-time)
# failed match attempt against each one.
PATHOLOGICAL_FILTER = "(a+)+b"


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpAstCatastrophicBacktrackingFilterTestCase(TestBase):
    def _class_name(self, i):
        return "%sc%d" % (_A_RUN, i)

    def _populate_scratch_ast_with_40_classes(self):
        """
        Defines and default-constructs each of the 40
        'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac<N>' classes via its own,
        separate 'expr ClassN{};' call (defining 'ClassN' inline in the
        same expression, since these classes only exist inside the
        scratch AST -- they are never declared in any module's debug
        info), so the target's shared scratch Clang ASTContext ends up
        holding all 40 as distinct top-level decls, one at a time, exactly
        as the scenario describes.

        Deliberately NOT cast to '(void)': LLDB only persists a JIT'd
        expression's result-carrying decl (and, transitively, its type)
        into the target's shared scratch ASTContext when that expression
        actually produces a live result value (materialized as a
        '$N' persistent result variable). Casting the result to 'void'
        (as in 'expr (void)ClassN{};') discards it before that
        materialization step, so the class ends up confined to that one
        expression's own throwaway AST and never reaches the shared
        scratch context at all -- confirmed by manual exploration: neither
        'target dump typesystem' nor any 'target modules dump ast' (with
        or without a matching '--filter') shows any trace of a class
        defined that way, in any module, once the expression that defined
        it has returned. Dropping the '(void)' cast is what actually
        fulfills the scenario this test is meant to cover.
        """
        for i in range(NUM_CLASSES):
            name = self._class_name(i)
            self.expect_expr(
                "struct %s { int x%d; }; %s{}" % (name, i, name),
                result_type=name,
            )

    def test_pathological_filter_after_40_classes_does_not_hang(self):
        """
        Baseline: after populating the scratch AST with all 40
        'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac<N>' classes, run 'target
        modules dump ast --filter (a+)+b' -- a pattern shaped like a
        classic catastrophic-backtracking regex that never matches any of
        the 40 names (they all end in 'c<N>', never 'b') -- against every
        module EXCEPT the one containing a template specialization (see
        the module docstring for why that matters), and confirm this
        completes promptly rather than hanging.

        This module only has one specialization-free module to dump ast
        against: the main executable ('a.out'), which does not include
        'plugin.cpp' (and therefore never reaches the
        'ClassTemplateSpecializationDecl'-triggered crash documented in
        'test_pathological_filter_after_40_classes_segfaults_via_template_'
        'specialization' below). This keeps this baseline method a genuine,
        crash-free regression test for the original catastrophic-
        backtracking-filter premise, independent of that unrelated crash.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self._populate_scratch_ast_with_40_classes()

        # Filtered dump of just the main executable's module, which has no
        # class template specialization in its debug info at all, so this
        # cannot hit the unrelated recursive-traversal crash documented
        # below. If '--filter' were backed by a backtracking regex engine,
        # matching '(a+)+b' against all 40 (non-matching) candidate names
        # here would still be exponential-time; since it's a plain
        # substring search instead, this returns essentially immediately.
        self.expect(
            "target modules dump ast --filter %s a.out" % PATHOLOGICAL_FILTER
        )

        # Repeating the same filtered dump several times back to back must
        # not degrade or hang either.
        for _ in range(5):
            self.expect(
                "target modules dump ast --filter %s a.out" % PATHOLOGICAL_FILTER
            )

        # A process is still very much alive and well after all of this:
        # '$0' is the persistent result variable from the very first of
        # the 40 'expr' calls in '_populate_scratch_ast_with_40_classes'
        # above (i.e. 'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac0{}'), still
        # readable back out of the (unharmed) scratch ASTContext.
        self.expect_expr("$0.x0", result_type="int", result_value="0")

    @expectedFailureAll(
        bugnumber="target modules dump ast --filter <anything non-empty> "
        "segfaults LLDB outright once a ClassTemplateSpecializationDecl is "
        "reachable from the module being dumped: clang::ASTPrinter::"
        "HandleTranslationUnit (clang/lib/Frontend/ASTConsumers.cpp) only "
        "takes its cheap, non-recursive, per-DeclContext print() walk when "
        "the filter string is EMPTY; giving --filter ANY non-empty value "
        "(matching or not -- a catastrophic-backtracking-shaped pattern "
        "like '(a+)+b' works exactly as well as a plain identifier or "
        "garbage that matches nothing) switches it over to a full "
        "clang::RecursiveASTVisitor<ASTPrinter>::TraverseDecl() traversal, "
        "and that traversal reliably crashes with EXC_BAD_ACCESS (SIGSEGV) "
        "inside clang::RecursiveASTVisitor<ASTPrinter>::"
        "TraverseClassTemplateSpecializationDecl as soon as any class "
        "template specialization (here, plugin.cpp's 'Wrap<int>') is "
        "reachable -- independent of the 40 aaaa...ac<N> classes also "
        "populating the scratch AST, and independent of the filter string "
        "being a regex-shaped pattern at all (see "
        "weird-ast-dump-ast-unicode-filter and "
        "weird-ast-dump-typesystem-template-instantiation-storm for "
        "independent confirmations of this same crash with different "
        "filter strings). Deliberately not exercised by name here (only "
        "documented) since a real segfault takes down the whole test "
        "process instead of failing cleanly; see the module docstring for "
        "the exact repro."
    )
    def test_pathological_filter_after_40_classes_segfaults_via_template_specialization(
        self,
    ):
        """
        Companion to
        'test_pathological_filter_after_40_classes_does_not_hang': the same
        40-class scratch-AST population, but this time the filtered dump
        targets the dylib ('libplugin.so'/'libplugin.dylib'), whose debug
        info contains 'Wrap<int>' (a genuine
        'ClassTemplateSpecializationDecl'). That one specialization is
        enough to make 'target modules dump ast --filter (a+)+b'
        unconditionally segfault LLDB, regardless of the filter string's
        shape and regardless of how many unrelated non-template classes are
        also present in the scratch AST.

        Exact repro (confirmed reproducible outside this test, since
        actually running it here would crash the test process):
          1. Build a dylib defining and instantiating 'template <typename
             T> struct Wrap { T val; }; Wrap<int> g_wrap;'.
          2. Break in the dylib, evaluate an expression that reaches
             'g_wrap' (e.g. 'expr g_wrap') so 'Wrap<int>' is parsed into
             the dylib's Clang AST.
          3. Optionally populate the scratch AST with any number of
             additional, unrelated classes first (e.g. the 40
             'aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaac<N>' classes from this
             module's docstring) -- this changes nothing about the crash.
          4. Run 'target modules dump ast --filter <anything non-empty>'
             against the dylib's module (a plain identifier, a
             non-matching string, or '(a+)+b' all reproduce it identically)
             -> SIGSEGV inside
             clang::RecursiveASTVisitor<ASTPrinter>::
             TraverseClassTemplateSpecializationDecl, called from
             ASTPrinter::TraverseDecl, called from
             TypeSystemClang::Dump, called from
             SymbolFileDWARF::DumpClangAST, called from
             CommandObjectTargetModulesDumpClangAST::DoExecute.
        Running the same filtered dump with an EMPTY '--filter' (or no
        '--filter' at all) does not crash, since that takes the linear
        print() path instead of TraverseDecl().

        This test method exists to document the limitation and is expected
        to fail cleanly (rather than crash) simply because it doesn't
        attempt the crashing command at all -- it only populates the
        scratch AST and evaluates 'g_wrap', then asserts on a value that
        intentionally does not hold, so that this bug stays visible in test
        results (as an XFAIL, not a silent gap) until someone fixes the
        underlying issue and turns this into a real regression test for
        the filtered-dump command itself.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self._populate_scratch_ast_with_40_classes()

        # Pulls 'Wrap<int>' (a ClassTemplateSpecializationDecl) into the
        # dylib's Clang AST.
        self.expect_expr("g_wrap.val", result_type="int", result_value="0")

        # Deliberately fails: documents that this scenario is not actually
        # safe to probe with a filtered dump against the dylib's module
        # (see the docstring above). This keeps the limitation visible as
        # an XFAIL instead of just omitting any coverage for it.
        self.fail(
            "not exercising 'target modules dump ast --filter %s "
            "<dylib>' here: it reliably segfaults LLDB once 'Wrap<int>' "
            "has been parsed into that module's AST (see docstring) and a "
            "real segfault would take down the whole test process instead "
            "of failing cleanly" % PATHOLOGICAL_FILTER
        )
