import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFilterRegexAcrossConflictingModulesTestCase(TestBase):
    def test_dump_ast_filter_across_all_modules_after_odr_merge(self):
        """
        Tests LLDB's handling of four dylibs that each define a
        same-named, but ODR-conflicting, 'struct Config':
          - DylibOne:   struct Config { int a; };
          - DylibTwo:   struct Config { int a; int b; };
          - DylibThree: struct Config { int a; int b; int c; };
          - DylibFour:  struct Config { int a; int b; int c; int d; };

        First, evaluating one expression per dylib that reads straight
        off each dylib's own global ('gConfigN.a') forces each of the
        four DWARFASTParserClang instances (one per dylib's debug info)
        to independently parse its own, mutually incompatible, version
        of 'Config' -- without yet pulling any of them into the shared
        per-target scratch AST context.

        Running 'target modules dump ast --filter Config' with *no*
        module argument at that point has to scan every loaded image
        (the main executable plus all four dylibs) and union the
        filter-matched results together, printing all four
        differently-shaped 'Config' completions back to back.

        Then, evaluating a single expression that reinterprets all four
        opaque 'void *' globals as 'Config *' and adds up their first
        field forces the ASTImporter to reconcile the four conflicting
        'Config' completions against each other (and against whichever
        one first got imported into the shared scratch AST context).
        Running the same filtered, no-module-argument 'dump ast' again
        immediately afterward -- and also 'target dump typesystem' to
        inspect the scratch AST context itself -- exercises the
        filter/union logic while the scratch TypeSystemClang holds
        whatever half-merged state resulted from that reconciliation. If
        the filter logic in the "dump ast" command cached or reused a
        QualType/Decl pointer keyed only by the name 'Config' across
        the four independent DWARFASTParserClang instances (instead of
        keeping each module's result separate), printing could end up
        dereferencing a Decl from the wrong module's ASTContext -- a
        cross-context pointer confusion bug that could crash LLDB. At a
        minimum, every command below must complete without crashing
        LLDB, regardless of whether the type information printed about
        the conflicting 'Config' definitions is faithful to any single
        dylib.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Force each of the four dylibs' own DWARFASTParserClang to parse
        # its own (mutually incompatible) 'Config' completion, one at a
        # time, via each dylib's own global variable.
        self.expect_expr("gConfig1.a", result_type="int", result_value="1")
        self.expect_expr("gConfig2.a", result_type="int", result_value="1")
        self.expect_expr("gConfig3.a", result_type="int", result_value="1")
        self.expect_expr("gConfig4.a", result_type="int", result_value="1")

        # With no module argument, this scans every loaded image (the
        # main executable and all four dylibs) and unions together every
        # match of the substring filter 'Config' -- which should include
        # all four independently-parsed, differently-shaped 'Config'
        # completions from the four dylibs above. This must not crash.
        self.expect("target modules dump ast --filter Config")

        # Force the ASTImporter to reconcile all four conflicting
        # 'Config' completions together, in a single expression, by
        # reinterpreting all four opaque pointers as 'Config *' and
        # reading/adding their first field. This pulls (a merged view of)
        # 'Config' into the shared per-target scratch AST context.
        self.expect(
            "expression (*(Config*)g1).a + (*(Config*)g2).a + "
            "(*(Config*)g3).a + (*(Config*)g4).a"
        )

        # Run the exact same filtered, no-module-argument dump again,
        # immediately after the merge above and without any further
        # expression evaluation in between, while the scratch
        # TypeSystemClang may still be holding whatever half-merged state
        # resulted from reconciling the four conflicting completions.
        self.expect("target modules dump ast --filter Config")

        # Separately, dump the shared per-target scratch
        # TypeSystem/ASTContext that the expression above actually
        # imported the (reconciled) 'Config' completion into.
        self.expect("target dump typesystem")

        # Exercise the merged state a bit more, in reverse pointer order,
        # and dump again in case corruption only shows up after further
        # use or after the underlying process has exited.
        self.expect(
            "expression (*(Config*)g4).a + (*(Config*)g3).a + "
            "(*(Config*)g2).a + (*(Config*)g1).a"
        )
        self.expect("target modules dump ast --filter Config")
        self.expect("target dump typesystem")
