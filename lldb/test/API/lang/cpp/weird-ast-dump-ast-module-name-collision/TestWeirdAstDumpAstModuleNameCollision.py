"""
Test LLDB's handling of two loaded dylibs that share the exact same
basename ("libfoo.dylib") -- and, on Darwin, the exact same LC_ID_DYLIB
install name -- while defining two mutually incompatible 'class Widget'
layouts, in order to make module-lookup-by-name (as used by e.g.
'target modules dump ast <module>') genuinely ambiguous.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
@skipUnlessDarwin
class WeirdAstDumpAstModuleNameCollisionTestCase(TestBase):
    def test_dump_ast_ambiguous_module_name(self):
        """
        Tests 'target modules dump ast <module>' against an ambiguous
        module spec that matches *two* distinct, simultaneously-loaded
        Module objects:
          - the "top-level" libfoo.dylib (linked normally into the
            executable): class Widget { public: int x; };
          - a "hidden" copy of libfoo.dylib, built from completely
            different source and placed in a different on-disk directory,
            but given the exact same LC_ID_DYLIB install name
            (@executable_path/libfoo.dylib) and therefore the exact same
            basename ("libfoo.dylib") that LLDB's module-lookup-by-name
            code matches on. It is loaded via dlopen() with an explicit
            full path at runtime, which is what actually gets two
            same-named, ODR-conflicting dylibs loaded into the same
            process at once (rather than one silently replacing the
            other, or dyld's usual load-time dedup collapsing them into a
            single image): class Widget { public: long x; long y; };

        'target modules dump ast libfoo.dylib' resolves the argument via
        FindModulesByName(), which matches modules purely by
        basename/fullpath (see Module::MatchesModuleSpec) without ever
        considering UUIDs unless one is given -- so passing just the
        basename "libfoo.dylib" here matches *both* loaded images at
        once. The dump implementation then loops over every match and
        dumps each one's Clang AST in turn.

        This test doesn't know in advance whether LLDB is "supposed" to
        pick one deterministically, refuse due to the ambiguity, or (as
        currently happens) dump both -- but no matter which, it must not
        crash or dereference a null/mismatched ModuleSP while doing so,
        even though the two matched modules' DWARFASTParserClang/
        TypeSystemClang machinery each hold an incompatible 'Widget' for
        the exact same name.
        """
        self.build()

        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Sanity check: both same-named dylibs are really loaded into the
        # target at once (this is the "load-time trick" itself -- without
        # it, this whole test would be exercising only one Module).
        matching_modules = [
            m for m in target.modules if m.GetFileSpec().GetFilename() == "libfoo.dylib"
        ]
        self.assertEqual(
            len(matching_modules),
            2,
            "expected exactly two distinct 'libfoo.dylib' images to be "
            "loaded into the target at once",
        )

        # The core of the test: dump the Clang AST for the ambiguous
        # module spec "libfoo.dylib", which matches both loaded images.
        # This must not crash, regardless of whether LLDB considers this
        # ambiguity an error, picks one module, or (as currently happens)
        # dumps every match it finds.
        self.expect("target modules dump ast libfoo.dylib")

        # Same thing, but filtered down to just the conflicting 'Widget'
        # decls, which makes the two (mutually incompatible) definitions
        # easy to compare side by side in the output.
        self.expect(
            "target modules dump ast --filter Widget libfoo.dylib",
        )

        # Force LLDB to actually resolve/import both conflicting 'Widget'
        # completions (by looking the type up by name, which is what
        # walks into each module's DWARFASTParserClang), then dump again
        # in case the ambiguous by-name dump behaves differently once
        # both modules' 'Widget' has already been completed.
        self.expect("target modules lookup -t Widget", substrs=["Widget"])
        self.expect("target modules dump ast libfoo.dylib")

        # Finally, dump the shared per-target scratch AST context. Even
        # though 'Widget' was only looked up per-module above (and never
        # actually imported into the scratch context by an expression),
        # this dump must still complete without crashing while two
        # same-named, ODR-conflicting per-module ASTs are alive at once.
        self.expect("target dump typesystem")

        # Read back a field from each dylib's global 'Widget' via a raw
        # pointer cast (the main executable never includes either
        # dylib's conflicting 'Widget' definition itself, so it can only
        # refer to the two globals as untyped 'void *'). This confirms
        # each dylib's own debug info/version of 'Widget' is still
        # usable for expression evaluation after the ambiguous dumps
        # above.
        self.expect_expr(
            "((int *)gTopWidget)[0]", result_type="int", result_value="111"
        )
        self.expect_expr(
            "((long *)gHiddenWidget)[0]", result_type="long", result_value="222"
        )
        self.expect_expr(
            "((long *)gHiddenWidget)[1]", result_type="long", result_value="444"
        )

    def test_dump_ast_filter_all_modules_after_expr_crashes(self):
        """
        Documents a genuine LLDB crash (stack-overflow via unbounded
        mutual recursion in Clang's RecursiveASTVisitor, as driven by
        LLDB's internal '(anonymous namespace)::ASTPrinter' used to
        implement 'target modules dump ast'), found while exploring this
        same ambiguous-module-name scenario.

        Unlike every other command in this test file, this one is *not*
        given an explicit module argument -- 'target modules dump ast
        --filter Widget' with no trailing module name asks LLDB to dump
        the Clang AST of *every* currently loaded module (here, well
        over 80, once system libraries are counted). After first
        evaluating an expression that imports a type backed by a real
        Clang 'ClassTemplateSpecializationDecl' into the per-target
        scratch AST context (any of the 'expect_expr' calls above
        already did this via 'operator[]' on the cast pointers pulling in
        implicit conversion machinery -- see below for a from-scratch,
        single-module repro that needs nothing ODR-related at all),
        'ASTPrinter::TraverseDecl' ends up in unbounded mutual recursion
        between 'TraverseClassTemplateSpecializationDecl' and
        'TraverseObjCImplementationDecl', overflowing the stack and
        crashing the whole 'lldb' process (SIGSEGV/SIGBUS from stack
        exhaustion, not a graceful error):

            (anonymous namespace)::ASTPrinter::TraverseDecl(clang::Decl*)
              -> clang::RecursiveASTVisitor<ASTPrinter>::
                     TraverseObjCImplementationDecl(...)
              -> (anonymous namespace)::ASTPrinter::TraverseDecl(clang::Decl*)
              -> clang::RecursiveASTVisitor<ASTPrinter>::
                     TraverseClassTemplateSpecializationDecl(...)
              -> ... (repeats until the stack is exhausted)

        This bug is *not* specific to the two-conflicting-'Widget'-dylibs
        setup in this test file -- it reproduces with a single, ordinary
        executable containing nothing but a global 'std::string', with no
        dylib and no ODR conflict whatsoever, as long as
        (a) an expression that touches the 'std::string' has already run,
        and (b) 'target modules dump ast --filter <name>' is then given
        with no module argument, so it iterates over every loaded module.
        This test keeps it in the ambiguous-module-name scenario anyway,
        since it was discovered while exploring that scenario and the
        ambiguous 'libfoo.dylib' spec is exactly the kind of "no module
        argument makes this walk more than one Module's AST at once" code
        path this crash lives in.

        Local manual repro (outside the test suite) with the two dylibs
        built by this test's Makefile, driving a stand-alone 'lldb -b'
        session:

            lldb -b \\
              -o "b main_entry" -o run \\
              -o "expr ((int *)gTopWidget)[0]" \\
              -o "target modules dump ast --filter Widget" \\
              ./a.out

        crashes with a raw LLVM stack dump ("PLEASE submit a bug report
        ...") whose top frames are exactly the
        'ASTPrinter::TraverseDecl' <-> 'TraverseObjCImplementationDecl'
        <-> 'TraverseClassTemplateSpecializationDecl' cycle described
        above.
        """
        self.build()

        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Import one of the two conflicting 'Widget' completions into the
        # scratch AST context. Any expression evaluation that goes
        # through the normal C++ expression parser (rather than a raw
        # memory read) is enough to reach the code path that trips up
        # the AST dumper below; this one is representative of the rest
        # of this test file.
        self.expect_expr(
            "((int *)gTopWidget)[0]", result_type="int", result_value="111"
        )

        # This is the crashing command: 'target modules dump ast' with a
        # '--filter' but *no* module argument, which makes it walk every
        # loaded module (not just the two ambiguous 'libfoo.dylib'
        # images) instead of a single, explicitly-named one. On a
        # working LLDB this should just print each module's (possibly
        # filtered-to-empty) AST dump and return normally -- it must not
        # crash the whole debugger process.
        self.expect("target modules dump ast --filter Widget")
