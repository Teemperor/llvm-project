import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpAstCycleUsingOdrTestCase(TestBase):
    def test_dump_ast_immediately_after_stopping_forces_nothing(self):
        """
        Baseline: stop in 'main' (before any code has run, and without
        evaluating any expression or looking up any type) and immediately
        run "target modules dump ast" against both the main executable and
        the dylib, then "target dump typesystem".

        Neither module's mutually-recursive 'A'/'B'/'SelfB' knot (see
        main.cpp/plugin.cpp) has been touched by anything yet at this
        point, so DWARFASTParserClang has not parsed any of it into
        TypeSystemClang: each per-module dump should show nothing but
        "<undeserialized declarations>", and the shared scratch
        TypeSystem/ASTContext (which only gets populated by expression
        evaluation or type lookups, neither of which has happened yet)
        should likewise be untouched by the ODR conflict. This must not
        crash, and pins down that "dump ast" on its own is a pure,
        non-mutating read of whatever has already been parsed -- it does
        not itself force any completion of the cyclic, ODR-violating
        'A'/'B' pair.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "main", lldb.SBFileSpec("main.cpp"))

        self.expect(
            "target modules dump ast a.out",
            substrs=["<undeserialized declarations>"],
        )
        self.expect(
            "target modules dump ast libplugin.dylib",
            substrs=["<undeserialized declarations>"],
        )
        self.expect("target dump typesystem")

    def test_dump_ast_after_forcing_completion_of_conflicting_cyclic_pair(self):
        """
        Tests LLDB's "target modules dump ast" and "target dump
        typesystem" commands against a mutually-recursive pair of types
        that is also a genuine ODR violation across two modules, and
        which additionally contains a self-referential 'using' alias
        that names the cyclic partner from inside the very type the
        partner points back to:

          - The main executable's pair (see main.cpp):
                struct B;
                struct A { B *pb; using SelfB = B; };
                struct B { A *pa; };

          - The dylib's pair (see plugin.cpp), same names and same
            mutually-recursive/aliasing shape, but both halves disagree
            in size with the exe's:
                struct B;
                struct A { B *pb; using SelfB = B; float extraA; };
                struct B { A *pa; int extra; };

        'A' contains a 'B *' and 'B' contains an 'A *', so completing
        either one on demand requires (at least partially) knowing about
        the other; 'A::SelfB' is a typedef, declared inside 'A', that
        names 'B' right back. Forcing completion of this knot via
        "target modules lookup -t" (deliberately *not* the expression
        evaluator -- this test wants DWARFASTParserClang's on-demand
        type completion alone, without also invoking Clang's parser/Sema
        on a user expression) for 'A', 'B', and 'A::SelfB' in both
        modules, back to back, and then dumping the AST for each module
        individually, then for all modules at once, is meant to stress
        whatever bookkeeping DWARFASTParserClang/TypeSystemClang use to
        track "currently being completed" decls across a self-referential
        cycle that also has an ODR conflict layered on top.

        Separately, dumping the shared scratch TypeSystem/ASTContext
        after evaluating expressions that reference both modules' 'A'
        globals forces the ASTImporter to import both same-named,
        differently-sized 'A'/'B'/'SelfB' knots into the same scratch
        AST at once. At a minimum, every command below must complete
        without crashing LLDB, regardless of whether the printed layout,
        merged scratch state, or alias resolution is faithful to any
        single module.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Force the main executable's own DWARFASTParserClang to
        # completely chase the 'A' -> 'B' -> 'A' cycle, plus the
        # self-referential 'A::SelfB' alias, independently of the
        # dylib's conflicting pair. None of this uses the expression
        # evaluator: "target modules lookup -t" resolves and completes
        # types directly.
        self.expect(
            "target modules lookup -t A a.out",
            substrs=["struct A", "pb"],
        )
        self.expect(
            "target modules lookup -t B a.out",
            substrs=["struct B", "pa"],
        )
        self.expect(
            "target modules lookup -t A::SelfB a.out",
            substrs=["typedef A::SelfB", "struct B", "pa"],
        )

        # Force the dylib's own (ODR-conflicting, differently-sized)
        # 'A' -> 'B' -> 'A' cycle and 'SelfB' alias to completely
        # resolve too, in the same session, right after the main
        # executable's above.
        self.expect(
            "target modules lookup -t A libplugin.dylib",
            substrs=["struct A", "pb", "extraA"],
        )
        self.expect(
            "target modules lookup -t B libplugin.dylib",
            substrs=["struct B", "pa", "extra"],
        )
        self.expect(
            "target modules lookup -t A::SelfB libplugin.dylib",
            substrs=["typedef A::SelfB", "struct B", "pa", "extra"],
        )

        # Dump the main executable's completed, self-referential
        # 'A'/'B'/'SelfB' knot on its own. This has to print the
        # mutually-recursive 'pb'/'pa' fields and the 'SelfB' alias
        # without recursing forever.
        self.expect("target modules dump ast a.out")

        # Immediately dump the dylib's differently-sized, equally
        # self-referential knot, in the same session, right after the
        # main executable's dump above.
        self.expect("target modules dump ast libplugin.dylib")

        # With no module argument, this scans every loaded image and
        # unions together every result -- printing both conflicting,
        # mutually-recursive 'A'/'B'/'SelfB' knots (main executable's and
        # the dylib's) back to back in a single pass. This must not
        # crash, regardless of whether the two printed layouts agree
        # with each other.
        self.expect("target modules dump ast")

        # Dump the shared per-target scratch TypeSystem/ASTContext.
        # Nothing has been imported into it yet (only per-module dumps
        # and type lookups have happened so far), so this just pins down
        # that the dump itself is harmless before any merging occurs.
        self.expect("target dump typesystem")

        # Now force the ASTImporter to import *both* conflicting 'A'
        # globals -- and therefore both conflicting, mutually-recursive
        # 'B's and 'SelfB' aliases -- into the single shared scratch
        # AST context, back to back.
        self.expect_expr("ga.pb", result_type="B *")
        self.expect_expr("gb.pb", result_type="B *")
        self.expect_expr("gb.extraA", result_type="float", result_value="1.5")

        # Dump the merged/half-merged scratch state left behind by the
        # two conflicting imports above.
        self.expect("target dump typesystem")

        # Exercise the merged state a bit more (an equality comparison
        # between two same-named, differently-sized 'A' pointer types
        # forces Sema to reason about which 'A' is meant on each side)
        # and dump again, in case corruption only shows up after further
        # use.
        self.expect("expression (A*)0 == (A*)0")
        self.expect("target modules dump ast")
        self.expect("target dump typesystem")
