"""
Test LLDB's expression evaluator against an ODR violation on an overloaded
operator: two modules each define an incompatible 'struct Cplx' (different
field types/counts) together with a matching 'operator+' overload for their
own 'Cplx'. The test stops with both modules' globals ('ca' from the main
executable, 'cb' from the dylib) simultaneously visible on the stack (one
calls into the other), which forces LLDB's DWARFASTParserClang/ASTImporter
machinery to import both conflicting 'Cplx' definitions -- and both
'operator+' overloads -- into the shared per-target scratch AST context
when evaluating expressions that reference both.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstOperatorOverloadOdrTestCase(TestBase):
    def setup_test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

    def test_each_alone(self):
        """
        Each module's 'Cplx'/'operator+' pair works fine when used on its
        own, even though the other (incompatible) definition of 'Cplx' is
        visible one frame up on the same stack.
        """
        self.setup_test()

        self.expect_expr("ca + ca", result_type="Cplx")
        self.expect_expr("cb + cb", result_type="Cplx")

    def test_mixed_expression(self):
        """
        Evaluates an expression that mixes 'ca' (main executable's
        'Cplx') and 'cb' (dylib's incompatible 'Cplx') in the same call
        to 'operator+'. Overload resolution should reject this: neither
        module's 'operator+' accepts the other module's 'Cplx', because
        LLDB keeps the two conflicting 'RecordDecl's for 'Cplx' distinct
        in the scratch AST context rather than silently merging them.
        This should never crash LLDB (e.g. via a mismatched struct-by-
        value ABI lowering in IRGen for the synthesized call), even
        though the two 'Cplx' definitions have different sizes and field
        types.
        """
        self.setup_test()

        self.expect(
            "expr ca + cb",
            error=True,
            substrs=["invalid operands to binary expression"],
        )

    def test_dump_typesystem_after_mixed_expression(self):
        """
        After an expression has forced both conflicting 'Cplx' decls into
        the scratch AST context, dumping the scratch type system should
        not crash LLDB, and should show both (structurally different)
        'Cplx' RecordDecls rather than a single corrupted/merged one.
        """
        self.setup_test()

        self.expect_expr("ca + ca", result_type="Cplx")
        self.expect_expr("cb + cb", result_type="Cplx")
        self.expect(
            "expr ca + cb",
            error=True,
            substrs=["invalid operands to binary expression"],
        )

        self.expect("target dump typesystem", substrs=["Cplx"])
