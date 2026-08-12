"""
Test LLDB's handling of a friend-injected free function ('peek') with
the same name/mangling pattern defined in both the main executable and a
dylib, where the enclosing class ('Box') that grants friendship has two
completely incompatible layouts across the two modules.

Friend declarations for free (non-member) functions are threaded into the
enclosing namespace/translation-unit DeclContext via a distinct mechanism
(a FriendDecl wrapping the NamedDecl) that is separate from the class's
own member list. ASTImporter has known fragility importing FriendDecl
since it must re-parent the friend into the translation-unit DeclContext
while the class DeclContext is still being completed. This test evaluates
an expression that reaches across the module boundary (casting one
module's variable to the other module's conflicting 'Box' type) and then
dumps the shared scratch typesystem, to see whether the friend-injected
lookup-table bookkeeping for the ODR-conflicting 'Box'/'peek' pair ends up
merely wrong, or crashes LLDB outright.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFriendInjectedMismatchOdrTestCase(TestBase):
    def test_box_alone_in_main_exe(self):
        """
        Looking at the main executable's 'Box' (a single private 'int
        secret', befriended by 'peek(Box&)') on its own should work fine,
        before the dylib's conflicting 'Box' is ever imported into the
        scratch AST context. This stops in 'main' itself (before
        plugin_entry/plugin_init are even called), so only the exe's
        'Box'/'peek' pair has debug info that has been requested so far.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "main", lldb.SBFileSpec("main.cpp"))

        self.expect_expr("peek(main_box)", result_type="int", result_value="1")

    def test_box_alone_in_dylib(self):
        """
        Looking at the dylib's 'Box' (a 'double secret' plus a 'long pad',
        also befriended by its own 'peek(Box&)') on its own should also
        work fine, before the main executable's conflicting 'Box' is ever
        imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("peek(plugin_box)", result_type="int", result_value="2")

    @expectedFailureAll(
        bugnumber="expr evaluator produces wrong-but-well-formed results and "
        "spurious 'Box is ambiguous' errors once both modules' "
        "friend-injected, ODR-conflicting 'Box'/'peek' pairs are pulled "
        "into the shared scratch AST context"
    )
    def test_cross_module_cast_and_dump_typesystem(self):
        """
        Tests LLDB's behaviour when the same class name 'Box' is:
          - defined in the main executable with a single private 'int
            secret' member and a friend-injected free function
            'peek(Box&)' that reads it, and
          - defined in a dylib with a completely incompatible layout
            ('double secret' + 'long pad') and its own, separately
            friend-injected 'peek(Box&)' with the identical
            name/mangling pattern.

        This is an ODR violation: the same type name has two mutually
        incompatible definitions, each with its own friend-injected free
        function threaded into the translation-unit DeclContext via
        Sema's FriendDecl machinery. After evaluating an expression that
        pulls each module's conflicting 'Box' into the target's shared
        scratch AST context (via DWARFASTParserClang/ASTImporter),
        evaluating an expression that casts one module's variable across
        the module boundary to the *other* module's conflicting 'Box'
        type exercises the friend-injected lookup-table bookkeeping for
        the ODR-conflicting pair. Dumping the shared scratch typesystem
        afterwards is the most likely place to observe any resulting
        corruption.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull the main executable's 'Box' (and its friend-injected
        # 'peek') into the scratch AST context.
        self.expect_expr("peek(main_box)", result_type="int", result_value="1")

        # Now pull the dylib's ODR-conflicting 'Box' (and its own,
        # separately friend-injected 'peek') into the same scratch AST
        # context.
        self.expect_expr("peek(plugin_box)", result_type="int", result_value="2")

        # Cast across the module boundary: reinterpret the dylib's
        # 'plugin_box' as the main executable's conflicting 'Box' layout
        # and call the main executable's friend-injected 'peek' on it.
        # This forces LLDB to resolve 'Box' and 'peek' against whichever
        # of the two ODR-conflicting definitions the expression parser
        # picks, while both are already loaded into the shared scratch
        # AST context.
        self.expect_expr("peek(*(Box*)&plugin_box)", result_type="int")

        # And the opposite direction: reinterpret the main executable's
        # 'main_box' as the dylib's conflicting 'Box' layout.
        self.expect_expr("peek(*(Box*)&main_box)", result_type="int")

        # Dumping the shared scratch typesystem after reaching across the
        # module boundary with both conflicting 'Box'/'peek' pairs loaded
        # is the most promising place to look for corruption in the
        # friend-injected lookup-table bookkeeping.
        self.expect("target dump typesystem")

        # Also dump each module's own per-module Clang AST filtered to
        # 'Box', to compare the (still separate, per-module) layouts.
        self.expect("target modules dump ast --filter Box")
