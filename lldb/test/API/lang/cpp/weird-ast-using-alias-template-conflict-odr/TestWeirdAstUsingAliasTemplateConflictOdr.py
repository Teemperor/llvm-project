"""
Deliberately puts LLDB's per-target shared scratch Clang AST into an
ODR-violating state via a type alias.

Both main.cpp and plugin.cpp define a class template 'Box' instantiated
with the exact same template argument ('int'), and both define a type
alias 'IntBox' naming that specialization -- but the two 'Box<int>'
bodies are structurally different (plugin.cpp's has an extra member).
This means 'IntBox' names two incompatible types depending on which
translation unit's debug info you're looking at.

This probes whether ASTImporter's TemplateSpecializationDecl dedup logic
(which is keyed on the underlying ClassTemplateDecl's identity plus
template-argument equality, without re-checking full structural
equivalence of the underlying RecordDecl's fields) conflates the two
'Box<int>' instantiations when both get imported into the shared scratch
AST context, and whether completing/laying out the conflated type
afterwards corrupts data or crashes LLDB.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstUsingAliasTemplateConflictOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each conflicting 'IntBox' instantiation can be evaluated fine on its
        own, as long as the *other* one hasn't already been imported into
        the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("ib.val", result_type="int", result_value="42")

    @expectedFailureAll(
        bugnumber="ASTImporter conflates two ODR-violating instantiations of "
        "the same class template specialization ('Box<int>') that are named "
        "via a type alias ('IntBox') with identical template arguments but "
        "structurally different bodies: once both are imported into the "
        "scratch AST context, the second one's extra fields get silently "
        "dropped from its reported type, so printing the full value of a "
        "variable of that type afterwards silently loses data instead of "
        "erroring out"
    )
    def test_both_together(self):
        """
        Tests LLDB's behavior when the same alias name ('IntBox') refers to
        two incompatible definitions of 'Box<int>': one in the main
        executable (only has 'val') and a different one (with an extra
        'extra' member) in a dylib.

        Using both conflicting definitions together in the same debug
        session shouldn't silently lose data, and ideally each should still
        evaluate with its own correct layout.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull the 4-byte 'Box<int>' (main.cpp's 'IntBox') into the scratch
        # AST context first.
        self.expect_expr("ib.val", result_type="int", result_value="42")

        # Now pull in the 8-byte 'Box<int>' (plugin.cpp's 'IntBox') via the
        # same alias name. This still works because it goes through the
        # dylib's own per-module AST rather than the (already-populated)
        # scratch AST context.
        self.expect_expr("ib2.extra", result_type="int", result_value="2")

        # Constructing via the bare alias name is inherently ambiguous as to
        # which 'Box<int>' instantiation it should target, but shouldn't
        # crash LLDB. In practice this reuses the first (4-byte) 'Box<int>'
        # that got imported into the scratch AST context above.
        self.expect_expr(
            "IntBox{99}",
            result_type="IntBox",
            result_children=[ValueCheck(name="val", value="99")],
        )

        # Ideally 'ib2' should still report both of its members ('val' and
        # 'extra') at this point: nothing about evaluating the above should
        # have changed what type describes 'ib2' in the debug info. But
        # because the scratch AST context now only knows about the 4-byte
        # 'Box<int>' (conflated with the alias 'IntBox'), printing 'ib2' as
        # a whole silently drops the 'extra' field instead of reporting
        # both members or erroring out.
        self.expect_expr(
            "ib2",
            result_type="IntBox",
            result_children=[
                ValueCheck(name="val", value="1"),
                ValueCheck(name="extra", value="2"),
            ],
        )
