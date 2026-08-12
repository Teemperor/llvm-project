import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDiamondInheritanceOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when a diamond-shaped class hierarchy
        ('Diamond' inheriting from 'B1' and 'B2') is defined with virtual
        inheritance in the main executable, but with plain (non-virtual)
        inheritance for the same-named classes in a dylib. This is an ODR
        violation, and virtual vs. non-virtual inheritance produces
        drastically different object layouts (extra vtable/vbase pointers
        for the virtual version). When both conflicting definitions of
        'Diamond' are used together in the same expression, LLDB's
        ASTImporter/TypeSystemClang machinery has to reconcile the two
        CXXRecordDecls, which could confuse Clang's record layout / vtable
        building code if it ends up laying out a Frankenstein-merged type.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Accessing the exe's virtually-inherited 'Diamond' on its own
        # shouldn't crash and should give sane values.
        self.expect_expr(
            "global_diamond.b1Field + global_diamond.b2Field + global_diamond.diamondField",
            result_type="int",
            result_value="6",
        )

        # Accessing the dylib's non-virtually-inherited 'Diamond' on its
        # own shouldn't crash either.
        self.expect_expr(
            "gPluginDiamond->b1Field + gPluginDiamond->b2Field + gPluginDiamond->diamondField",
            result_type="int",
            result_value="60",
        )

        # Now use both conflicting definitions of 'Diamond' together in the
        # same expression. This forces LLDB to import/reconcile both
        # CXXRecordDecls (virtual-base vs. non-virtual-base 'Diamond') into
        # the same AST context, which is the interesting/dangerous part of
        # this test: at worst this should give a benign (if wrong) result,
        # but it should never crash LLDB.
        self.expect_expr(
            "global_diamond.diamondField + gPluginDiamond->diamondField",
            result_type="int",
            result_value="33",
        )

        # Also try 'image lookup' for the type name, which independently
        # exercises the DWARFASTParserClang/TypeSystemClang path for
        # completing 'Diamond' from debug info.
        self.expect("image lookup -t Diamond", substrs=["Diamond"])
