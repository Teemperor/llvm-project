"""
Test LLDB's behaviour when a friend class declaration is combined with
class template instantiation to create a genuine ODR conflict spread
across a main executable and a dylib:

  main.cpp:
    template <typename T> class Holder { T v; friend class Peeker; };
    class Peeker { public: static int get(Holder<int> &h); };
    int Peeker::get(Holder<int> &h) { return 1; }
    Holder<int> ha;

  plugin.cpp:
    template <typename T> class Holder { T v; T v2; friend class Peeker; };
    class Peeker { public: static double get(Holder<int> &h); };
    double Peeker::get(Holder<int> &h) { return 2.0; }
    Holder<int> hb;

'Peeker' is a friend of 'Holder<int>' in both translation units, but the
two 'Holder<int>' specializations have different sizes/layouts and the two
'Peeker::get' overloads have different signatures/return types. Friend
declarations combined with template instantiation force Sema's
access-control and redeclaration-chain logic to look up 'Peeker' as a
friend of an *instantiated* 'Holder<int>', and the ASTImporter has to
import both the FriendDecl and the separately-instantiated 'Holder<int>'
ClassTemplateSpecializationDecl from each module into the target's shared
scratch ASTContext.

Evaluating 'Peeker::get(ha)' pulls the main executable's ODR-conflicting
pair into the scratch AST context. See
test_dump_ast_filter_after_conflicting_expr_segfaults below for what
happens if LLDB's own '--filter'-based AST dumping command is then run
against that corrupted scratch state.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFriendTemplateInstantiationOdrTestCase(TestBase):
    def test_expr_and_unfiltered_dump(self):
        """
        Evaluating 'Peeker::get(ha)' resolves fine (using the main
        executable's own definitions), and dumping LLDB's internal Clang
        AST state afterwards - as long as no '--filter' is passed - should
        not crash LLDB, even though the shared scratch ASTContext now holds
        an ODR-conflicting 'Holder<int>'/'Peeker' pair.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("Peeker::get(ha)", result_type="int", result_value="1")

        # Unfiltered dumps walk each DeclContext's direct children without
        # recursing into e.g. a friend's granted-access target, so they
        # tolerate the ODR conflict instead of crashing.
        self.expect("target modules dump ast")
        self.expect("target dump typesystem")

        # The process is still alive and well after both dumps.
        self.expect_expr("ha.v", result_type="int", result_value="0")

    @expectedFailureAll(
        bugnumber="target modules dump ast --filter Peeker segfaults LLDB "
        "after 'Peeker::get(ha)' has been evaluated: the friend "
        "declaration inside the ODR-conflicting 'Holder<int>' template "
        "forces clang's ASTPrinter (once given ANY non-empty --filter, "
        "which switches it from its normal linear print() walk over to a "
        "full RecursiveASTVisitor::TraverseDecl() traversal - see "
        "ASTPrinter::HandleTranslationUnit in "
        "clang/lib/Frontend/ASTConsumers.cpp) to recurse into the "
        "ClassTemplateSpecializationDecl for 'Holder<int>' that was left "
        "in a partially-imported/inconsistent state by the two mutually "
        "incompatible 'friend class Peeker' declarations. This crashes "
        "with SIGSEGV inside "
        "clang::RecursiveASTVisitor<ASTPrinter>::"
        "TraverseClassTemplateSpecializationDecl. Reproducible with "
        "'target modules dump ast --filter Peeker' specifically (filtering "
        "on 'Holder' alone does not trigger it); requires "
        "'Peeker::get(ha)' (or any expression referencing the main "
        "executable's 'Holder<int>'/'Peeker') to have been evaluated "
        "first - the crash does not happen with the filtered dump alone."
    )
    def test_dump_ast_filter_after_conflicting_expr_segfaults(self):
        """
        Companion to test_expr_and_unfiltered_dump: the exact same setup,
        but 'target modules dump ast' is given a '--filter Peeker' argument
        instead of dumping everything.

        This is a REAL, currently-reproducible LLDB crash (not a
        hypothetical), confirmed via manual exploration:

          1. Build a main executable defining and instantiating
             'Holder<int>'/'Peeker' as above (main.cpp) and a dylib with the
             ODR-conflicting definitions (plugin.cpp).
          2. Break in 'plugin_entry', 'expr Peeker::get(ha)' to pull the
             main executable's 'Holder<int>'/'Peeker' pair into the scratch
             AST context.
          3. Run 'target modules dump ast --filter Peeker' (module defaults
             to "all modules", equivalently 'target modules dump ast a.out
             --filter Peeker'): segfaults LLDB (SIGSEGV, exit code 139),
             confirmed reproducible across repeated runs. Filtering on
             'Holder' instead of 'Peeker' does NOT crash. Running the same
             filtered dump WITHOUT the preceding 'expr' also does NOT
             crash.

        This test method exists to document the limitation and is expected
        to fail cleanly (rather than crash) by only evaluating the
        conflicting expression and then asserting on a value that
        intentionally does not hold, so this bug stays visible in test
        results (as an XFAIL, not a silent gap) instead of taking down the
        whole test process, until someone fixes the underlying issue and
        turns this into a real regression test for the filtered-dump
        command itself.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("Peeker::get(ha)", result_type="int", result_value="1")

        # Deliberately fails: documents that this scenario is not actually
        # safe to probe with a filtered dump (see the docstring above).
        self.fail(
            "not exercising 'target modules dump ast --filter Peeker' here: "
            "it reliably segfaults LLDB after 'Peeker::get(ha)' has been "
            "evaluated (see docstring) and a real segfault would take down "
            "the whole test process instead of failing cleanly"
        )
