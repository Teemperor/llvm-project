"""
Test LLDB's handling of a friend-injected free function ('trace') with
the exact same declared signature ('int trace(const Matrix&)') defined
in both the main executable and a dylib, where the enclosing class
('Matrix') that grants friendship has two completely incompatible
layouts (a 2x2 matrix stored as 4 ints vs. a 3x3 matrix stored as 9
ints) across the two modules.

Friend declarations for free (non-member) functions are threaded into
the enclosing namespace/translation-unit DeclContext via a distinct
mechanism (a FriendDecl wrapping the NamedDecl) that is separate from
the class's own member list. ASTImporter has known fragility importing
FriendDecl since it must re-parent the friend into the translation-unit
DeclContext while the class DeclContext is still being completed.
Because both modules' 'trace' free functions have byte-for-byte
identical declared signatures, Clang's redeclaration lookup in the
merged scratch context could plausibly treat them as "the same"
function once both are visible, rather than flagging an ODR conflict.

This test evaluates expressions that reach across the module boundary
(casting one module's variable to the other module's conflicting
'Matrix' type) and dumps the shared scratch typesystem, to see whether
the friend-injected lookup-table bookkeeping for the ODR-conflicting
'Matrix'/'trace' pair ends up merely wrong, or crashes LLDB outright.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstFriendFreefuncSameNameConflictingClassOdrTestCase(TestBase):
    def test_matrix_alone_in_main_exe(self):
        """
        Looking at the main executable's 'Matrix' (a 2x2 matrix stored as
        a 4-int 'data' array, befriended by 'trace(const Matrix&)') on
        its own should work fine, before the dylib's conflicting 'Matrix'
        is ever imported into the scratch AST context. This stops in
        'main' itself (before plugin_entry/plugin_init are even called),
        so only the exe's 'Matrix'/'trace' pair has debug info that has
        been requested so far.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "main", lldb.SBFileSpec("main.cpp"))

        # data = {1, 2, 3, 4} -> data[0] + data[3] == 1 + 4 == 5
        self.expect_expr("trace(ma)", result_type="int", result_value="5")

    def test_matrix_alone_in_dylib(self):
        """
        Looking at the dylib's 'Matrix' (a 3x3 matrix stored as a 9-int
        'data' array, also befriended by its own 'trace(const Matrix&)')
        on its own should also work fine, before the main executable's
        conflicting 'Matrix' is ever imported into the scratch AST
        context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # data = {1, ..., 9} -> data[0] + data[4] + data[8] == 1 + 5 + 9 == 15
        self.expect_expr("trace(mb)", result_type="int", result_value="15")

    @expectedFailureAll(
        bugnumber="expr evaluator produces wrong-but-well-formed results and "
        "spurious 'Matrix is ambiguous' errors once both modules' "
        "friend-injected, ODR-conflicting 'Matrix'/'trace' pairs (which "
        "share a byte-for-byte identical 'int trace(const Matrix&)' "
        "signature) are pulled into the shared scratch AST context"
    )
    def test_cross_module_cast_and_dump_typesystem(self):
        """
        Tests LLDB's behaviour when the same class name 'Matrix' is:
          - defined in the main executable as a 2x2 matrix (4-int 'data')
            with a friend-injected free function 'trace(const Matrix&)'
            that sums data[0] + data[3], and
          - defined in a dylib as a completely incompatible 3x3 matrix
            (9-int 'data') with its own, separately friend-injected
            'trace(const Matrix&)' -- the exact same declared signature
            -- that sums data[0] + data[4] + data[8].

        This is an ODR violation: the same type name has two mutually
        incompatible definitions, each with its own friend-injected free
        function (with an identical signature) threaded into the
        translation-unit DeclContext via Sema's FriendDecl machinery.
        After evaluating an expression that pulls each module's
        conflicting 'Matrix' into the target's shared scratch AST
        context (via DWARFASTParserClang/ASTImporter), evaluating an
        expression that reinterprets one module's variable across the
        module boundary as the *other* module's conflicting 'Matrix'
        layout exercises the friend/redeclaration lookup-table
        bookkeeping for the ODR-conflicting pair: it forces the
        expression parser to pick one specific 'trace' FunctionDecl while
        the argument's real underlying type disagrees with the parameter
        type that was picked. Dumping the shared scratch typesystem
        afterwards is the most likely place to observe any resulting
        corruption.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull the main executable's 'Matrix' (and its friend-injected
        # 'trace') into the scratch AST context.
        self.expect_expr("trace(ma)", result_type="int", result_value="5")

        # Now pull the dylib's ODR-conflicting 'Matrix' (and its own,
        # separately friend-injected 'trace' with the identical
        # signature) into the same scratch AST context.
        self.expect_expr("trace(mb)", result_type="int", result_value="15")

        # Reinterpret the dylib's real 9-int 'mb' through the main
        # executable's 4-int 'Matrix' layout, and call 'trace' on it.
        # This forces LLDB to resolve 'Matrix' and 'trace' against
        # whichever of the two ODR-conflicting definitions the
        # expression parser picks, while both are already loaded into
        # the shared scratch AST context.
        self.expect_expr("trace(*(Matrix*)&mb)", result_type="int")

        # And the opposite direction: reinterpret the main executable's
        # real 4-int 'ma' as the dylib's 9-int 'Matrix' layout.
        self.expect_expr("trace(*(Matrix*)&ma)", result_type="int")

        # Dumping the shared scratch typesystem after reaching across the
        # module boundary with both conflicting 'Matrix'/'trace' pairs
        # loaded is the most promising place to look for corruption in
        # the friend-injected lookup-table bookkeeping.
        self.expect("target dump typesystem")

        # Also dump each module's own per-module Clang AST filtered to
        # 'Matrix', to compare the (still separate, per-module) layouts.
        self.expect("target modules dump ast --filter Matrix")
