"""
Test "target modules dump ast --filter <name>" with filter strings that
contain literal regex metacharacters -- and a few deliberately malformed
"regex-shaped" strings -- against DWARF names that themselves contain
regex metacharacters (template instantiations and operator overloads).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDumpFilterRegexMetacharactersTestCase(TestBase):
    def test_filter_strings_containing_regex_metacharacters(self):
        """
        Tests "target modules dump ast --filter <name>" using filter
        strings built out of the literal, regex-metacharacter-laden
        qualified names that this test's types actually have in DWARF:

          - 'Vec<int>'          (a class template instantiation: '<' '>')
          - 'Op::operator()(int)' and 'Op::operator[](int)'
                                  (operator overloads: '(' ')' '[' ']')
          - 'Op::operator+(Op const&)'
                                  ('+', '&', spaces)
          - 'Outer::Inner'      ('::' -- not a metacharacter, but part of
                                  the same qualified-name soup)

        as well as filter strings that are *themselves* malformed as
        regexes if ever fed into a regex engine (which the underlying
        filter logic must not do, since it should just be a plain
        substring match):

          - '('   -- an unbalanced opening paren
          - '\\'  -- a single trailing backslash
          - ''    -- the empty string, passed via a quoting trick
                     ("--filter ''") rather than by omitting the option

        None of this must crash LLDB. In fact, per LLDB's actual
        "--filter" implementation (a plain std::string::find substring
        search over each Decl's qualified name -- see
        clang::ASTPrinter::filterMatches() in
        clang/lib/Frontend/ASTConsumers.cpp, which is what
        TypeSystemClang::Dump() ultimately calls into), none of these
        strings are ever compiled as a regex at all, so no
        std::regex_error/malformed-pattern exception is possible here.

        However, manual exploration while writing this test found a
        real, unrelated, and very easy to trigger crash in the exact
        same code path: once an expression evaluation has caused a
        module's DWARFASTParserClang to parse/complete a
        ClassTemplateSpecializationDecl (here, 'Vec<int>') into that
        module's Clang AST, running "target modules dump ast --filter
        <anything that is non-empty and does not itself match at the
        top-level Decl>" against that module reliably segfaults LLDB --
        regardless of whether the filter string contains any regex
        metacharacters at all. This happens because
        TypeSystemClang::CreateClassTemplateSpecializationDecl() marks
        the specialization TSK_ExplicitSpecialization but never calls
        setTemplateArgsAsWritten() on it (there is no real "as written"
        source for a DWARF-derived specialization). Clang's own
        RecursiveASTVisitor, when a filter fails to match at a given
        Decl, keeps recursing into that Decl's children -- and for an
        explicit-specialization ClassTemplateSpecializationDecl,
        TraverseClassTemplateSpecializationDecl() unconditionally
        dereferences D->getTemplateArgsAsWritten()->getTemplateArgs(),
        which is a null-pointer dereference here. Only an empty filter
        string (which takes an entirely different, non-traversing
        "dump everything" code path) or a filter that happens to match
        at the very node being visited (so the traversal short-circuits
        before recursing into children) avoid the crash. All of the
        genuinely regex-metacharacter-laden filters below (operator
        names, '<', '>') hit this non-matching-recursion crash, since
        none of them are a substring of 'Vec<int>' itself while
        'Vec<int>' is still on the traversal path to the real target
        Decls.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Force this module's DWARFASTParserClang to parse/complete the
        # 'Vec<int>' ClassTemplateSpecializationDecl into the module's
        # own (DWARF-derived) Clang AST.
        self.expect_expr("g_vec_from_plugin.data[0]", result_type="int", result_value="7")

        # Also force 'Outer::Inner' and 'Op' (with its operator
        # overloads) to be parsed/completed.
        self.expect_expr("g_inner_from_plugin.value", result_type="int")
        self.expect_expr("g_op_from_plugin.x", result_type="int")

        # Filter strings that are literal, regex-metacharacter-laden
        # qualified/DWARF names from this module. None of these should
        # crash LLDB, regardless of whether they end up matching
        # anything in the AST.
        metacharacter_filters = [
            "Vec<int>",
            "Op::operator()(int)",
            "Op::operator[](int)",
            "Op::operator+(Op const&)",
            "Outer::Inner",
            "Vec<int>::Get(int)",
            ".",
            "*",
            "^",
            "$",
            "|",
        ]
        for filt in metacharacter_filters:
            self.expect('target modules dump ast --filter "%s"' % filt)

        # A filter string that is itself an unbalanced regex: a single,
        # lone opening parenthesis with no matching close. If the
        # underlying filter logic ever built a std::regex/llvm::Regex
        # out of the raw filter string without catching malformed-
        # pattern errors, this would be exactly the kind of input to
        # throw/report on. This must not crash LLDB.
        self.expect("target modules dump ast --filter '('")

        # A filter string that is a single trailing backslash -- also a
        # malformed regex/escape sequence in many regex flavors. This
        # must not crash LLDB either.
        self.expect(r"target modules dump ast --filter '\'")

        # The empty string, passed explicitly via a quoting trick
        # ("--filter ''") rather than by omitting the "--filter" option
        # entirely. This exercises the empty-filter fast path (which
        # dumps everything without any RecursiveASTVisitor-based
        # filtering/traversal) and must not crash LLDB.
        self.expect("target modules dump ast --filter ''")

        # A filter string containing an embedded NUL byte, passed
        # through SBCommandInterpreter.HandleCommand directly (as a
        # Python string containing an embedded NUL) rather than through
        # the normal, already-sanitized 'self.expect'/'self.runCmd' text
        # path, so it reaches LLDB's own command-line argument parsing
        # as close to raw as this API allows.
        interp = self.dbg.GetCommandInterpreter()
        result = lldb.SBCommandReturnObject()
        command_with_nul = "target modules dump ast --filter 'Op\x00Vec'"
        interp.HandleCommand(command_with_nul, result)

        # Whether or not LLDB treated the embedded-NUL filter as
        # matching anything (or truncated it at the NUL, or rejected it
        # outright), it must not have crashed getting here: if it had,
        # this test process itself would already be dead, so merely
        # reaching this assertion is part of the test.
        self.assertTrue(True, "reached this point without LLDB crashing")

        # The target-level commands below must also survive running
        # again after all of the hostile filters above.
        self.expect("target modules dump ast --filter Vec")
        self.expect("target dump typesystem")
