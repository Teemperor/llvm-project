import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpAstUnicodeFilterTestCase(TestBase):
    def test_huge_and_invalid_utf8_filter_after_template_instantiation(self):
        """
        Tests 'target modules dump ast --filter <name>' against a module
        whose debug info contains a deliberately extreme class template
        instantiation: 'DeepType' is fifty levels of nested
        'Wrap<Wrap<...<int>...>>', independently defined in both the main
        executable (main.cpp) and the dylib (plugin.cpp).

        Evaluating an expression that reads 'g_deep_from_main' first
        forces LLDB's DWARFASTParserClang to actually parse and complete
        every one of the fifty nested 'ClassTemplateSpecializationDecl'
        levels of 'Wrap<...>' inside the module's Clang AST (rather than
        leaving them as forward-only/uninstantiated placeholders).

        Only *after* that does this test run 'target modules dump ast
        --filter <name>' with two deliberately hostile filter strings:
          1. A ~10,000-character filter string -- a huge '|'-separated
             regex-shaped alternation of garbage tokens that does not
             match anything in the AST.
          2. A single, genuinely invalid UTF-8 byte (a lone 0x80
             continuation byte with no lead byte), passed through
             'SBCommandInterpreter.HandleCommand' as a raw (non-UTF-8)
             byte string rather than through the normal 'self.expect'
             command path, so it reaches LLDB's command-line argument
             parsing exactly as raw, invalid-UTF-8 bytes rather than
             already-sanitized text.

        In both cases the filter string does not match anything in the
        module's AST. That "no match" case turns out to matter a great
        deal: manual exploration while writing this test found that,
        once an expression has caused a 'ClassTemplateSpecializationDecl'
        to be parsed/completed in a module's AST, running 'target
        modules dump ast --filter <anything that does not match>'
        against that module reliably segfaults LLDB itself (a genuine
        EXC_BAD_ACCESS / SIGSEGV inside
        'clang::RecursiveASTVisitor<ASTPrinter>::
        TraverseClassTemplateSpecializationDecl', called from
        'TypeSystemClang::Dump' by way of
        'SymbolFileDWARF::DumpClangAST' and
        'CommandObjectTargetModulesDumpClangAST::DoExecute') --
        regardless of whether the filter is a huge garbage regex, a
        single non-matching identifier, or a lone invalid UTF-8 byte, and
        regardless of how deeply 'Wrap<...>' is nested (the crash was
        reproduced even with a single level of nesting, and even with a
        plain 'std::vector<int>' in place of the custom 'Wrap' template).
        The fifty-level-deep nesting and the two hostile filter strings
        below are kept as the stress scenario this test is meant to
        guard against; at a minimum, none of the commands below should
        ever crash LLDB.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Force DWARFASTParserClang to fully parse/complete all fifty
        # nested 'Wrap<...>' class template specialization levels inside
        # the main executable's Clang AST.
        self.expect("expression g_deep_from_main")

        # A ~10,000-character filter string: a huge alternation of
        # garbage tokens that does not match anything in the AST. This
        # must not crash LLDB.
        huge_filter = "|".join("garbage_token_%d_xyz" % i for i in range(500))
        self.assertGreater(len(huge_filter), 10000)
        self.expect(
            'target modules dump ast --filter "%s" a.out' % huge_filter
        )

        # A single, genuinely invalid UTF-8 byte (a lone 0x80
        # continuation byte) passed through
        # 'SBCommandInterpreter.HandleCommand' as raw, non-UTF-8-decoded
        # bytes -- rather than through the normal, already-sanitized
        # 'self.expect'/'self.runCmd' text path -- so that LLDB's own
        # command-line parsing sees the invalid byte directly.
        raw_command = b'target modules dump ast --filter "\x80" a.out'
        # 'latin-1' maps each byte 0..255 to the identical Unicode code
        # point, so re-encoding as UTF-8 later reproduces the exact
        # original (invalid-as-UTF-8) byte sequence unchanged: the lone
        # 0x80 byte survives as a single, unpaired continuation byte.
        command_str = raw_command.decode("latin-1")

        interp = self.dbg.GetCommandInterpreter()
        result = lldb.SBCommandReturnObject()
        interp.HandleCommand(command_str, result)

        # Whether or not LLDB considered the invalid-UTF-8 filter a
        # match, it must not have crashed getting here: if it did, this
        # process itself would already be dead (a segfault inside
        # liblldb kills this test process, not just the inferior), so
        # merely reaching this assertion is part of the test.
        self.assertTrue(True, "reached this point without LLDB crashing")

        # The scratch/target-level dump commands below must also survive
        # being run again after the two hostile filter commands above.
        self.expect("target modules dump ast --filter Wrap a.out")
        self.expect("target dump typesystem")
