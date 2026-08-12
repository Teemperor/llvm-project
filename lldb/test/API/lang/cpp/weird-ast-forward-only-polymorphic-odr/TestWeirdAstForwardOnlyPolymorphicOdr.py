import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstForwardOnlyPolymorphicOdrTestCase(TestBase):
    def test(self):
        """
        Tests evaluating expressions against a type ('Opaque') that is only
        ever forward-declared in the main executable's debug-info (it holds
        an 'Opaque *' global but never sees a complete definition of
        'Opaque' itself) while the *only* complete, polymorphic (i.e., it
        has a vtable) definition of 'Opaque' lives in a separate dylib.

        There is no ODR conflict here: there is exactly one true definition
        of 'Opaque' in the whole program. This is meant to exercise (and
        stress) the baseline cross-module "find the complete definition for
        this forward-declared DIE in some other module" path inside
        DWARFASTParserClang/ASTImporter, independent of any conflicting
        layout. If that path is fragile even in this conflict-free case, it
        should crash or misbehave here already.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Evaluate expressions that force LLDB to resolve 'Opaque' starting
        # from the main executable's forward-declaration-only context, which
        # requires completing the type by finding its real definition in the
        # dylib's debug-info.
        self.expect_expr("gOpaquePtr != nullptr", result_value="true")
        self.expect_expr("gOpaquePtr->getValue()", result_value="42")
        self.expect_expr("gOpaquePtr->value", result_value="42")
        self.expect_expr("*gOpaquePtr")
        self.expect("expression sizeof(*gOpaquePtr)")
        self.expect("target variable gOpaquePtr", substrs=["gOpaquePtr"])
