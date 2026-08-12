"""
Test LLDB's behaviour when the same class template specialization
'Secret<int>' is defined with a friend declaration ('friend struct Peek;')
in the main executable, but WITHOUT any friend declaration at all for the
same-named (but unrelated) 'struct Peek' in a dylib.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFriendTemplateInjectionOdrTestCase(TestBase):
    def test_secret_alone_in_main_exe(self):
        """
        Looking at the main executable's 'Secret<int>' (whose 'value'
        member is private, but accessible via a friend 'Peek::get()') on
        its own should work fine, before the dylib's conflicting
        friend-less 'Secret<int>' is ever imported into the scratch AST
        context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_secret.value", result_type="int", result_value="42")
        self.expect_expr("Peek::get(main_secret)", result_type="int", result_value="42")

    def test_secret_alone_in_dylib(self):
        """
        Looking at the dylib's 'Secret<int>' (which has no friend
        declaration at all, and an unrelated same-named 'Peek') on its own
        should also work fine, before the main executable's conflicting
        'Secret<int>' (with its FriendDecl for 'Peek') is ever imported
        into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("plugin_secret.value", result_type="int", result_value="99")

    def test_dump_ast_after_importing_conflicting_secret_specializations(self):
        """
        Tests LLDB's behaviour when the same template-id 'Secret<int>' is:
          - defined in the main executable with a FriendDecl granting
            'struct Peek' access to its private 'value' member, and
          - defined in a dylib with the identical shape but NO friend
            declaration at all, alongside a same-named but semantically
            unrelated 'struct Peek'.

        This is an ODR violation: the same specialization has two
        incompatible friend-list states across translation units. After
        evaluating expressions that pull each module's conflicting
        'Secret<int>' ClassTemplateSpecializationDecl into the target's
        shared scratch AST context (via DWARFASTParserClang/ASTImporter),
        dumping a module's Clang AST filtered by an unrelated name (here,
        'Peek', which does not match the imported 'Secret'
        ClassTemplateSpecializationDecl) forces LLDB's ASTPrinter to
        traverse into that decl anyway while looking for filter matches.
        The hope is that this traversal, combined with the ODR-conflicting
        friend/access-control bookkeeping carried on the imported
        RecordDecl, crashes LLDB outright (e.g. an assertion or segfault
        inside Sema's access-control machinery or Clang's
        RecursiveASTVisitor) instead of merely producing a wrong-but-
        well-formed value.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull the main executable's friend-carrying 'Secret<int>' into the
        # scratch AST context.
        self.expect_expr("main_secret.value", result_type="int", result_value="42")

        # Now pull the dylib's friend-less, ODR-conflicting 'Secret<int>'
        # into the same scratch AST context.
        self.expect_expr("plugin_secret.value", result_type="int", result_value="99")

        # Dumping a module's AST filtered by an unrelated name ('Peek')
        # still has to traverse past the (filter-non-matching) imported
        # 'Secret<int>' ClassTemplateSpecializationDecl while looking for
        # matches. This should never crash LLDB, no matter how
        # inconsistent the merged friend-list bookkeeping for 'Secret<int>'
        # has become.
        self.expect("target modules dump ast --filter Peek")

        # Also try combining both conflicting definitions in a single
        # expression, and dumping the shared scratch typesystem afterwards.
        self.expect_expr(
            "Peek::get(main_secret) + plugin_secret.value",
            result_type="int",
            result_value="141",
        )
        self.expect("target dump typesystem")
