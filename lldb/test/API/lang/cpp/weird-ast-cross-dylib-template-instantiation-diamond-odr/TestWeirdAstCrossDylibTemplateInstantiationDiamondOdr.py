"""
Test LLDB's handling of an ODR conflict on a class template instantiation
('Box<int>') that reaches the debugger from two different dylibs, where the
conflict is hidden behind a non-type template default argument rather than
a directly visible difference in the source.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstCrossDylibTemplateInstantiationDiamondOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each dylib's 'Box<int>' can be read fine on its own, as long as the
        *other* dylib's conflicting 'Box<int>' hasn't already been imported
        into the shared per-target scratch AST context.

        Left.h and Right.h both define an identically-shaped class
        template:
            template <typename T> struct Box {
              T val;
              int tag;
              char pad[PAD_SIZE];
            };
        where PAD_SIZE is a macro standing in for a non-type template
        default argument that got resolved differently on each side of a
        "diamond" header include: Left.h's PAD_SIZE is 1 and Right.h's is
        4. Since 'Box' only has a single (type) template parameter in
        each header, both dylibs' 'Box<int>' instantiations end up with
        the exact same name/mangling in the debug info, despite
        disagreeing about sizeof(Box<int>) and the layout of the trailing
        'pad' member.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("gLeftBox->pad[0]", result_type="char", result_value="'L'")

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile two genuinely ODR-violating "
        "specializations of the same class template that are both named "
        "'Box<int>' in the debug info (same template arguments, but a "
        "different effective array bound on the trailing 'pad' member and "
        "therefore a different layout): whichever specialization is "
        "imported into the scratch AST context first wins, and referring "
        "to the other one afterwards fails with 'undeclared identifier'"
    )
    def test_both_together(self):
        """
        Tests LLDB's behavior when the exact same-looking template
        specialization ('Box<int>') has two incompatible layouts: Left's
        dylib effectively has 'char pad[1]' and Right's dylib effectively
        has 'char pad[4]', but both are named 'Box<int>' in the debug info.

        Using both conflicting specializations together in the same
        expression shouldn't crash, and ideally should evaluate correctly.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr(
            "gLeftBox->pad[0] + gRightBox->pad[0]",
            result_type="int",
            result_value=str(ord("L") + ord("R")),
        )

    def test_dump_ast_and_typesystem(self):
        """
        Regression test for LLDB's internal Clang AST dumping commands
        ('target modules dump ast --filter Box' and 'target dump
        typesystem') not crashing while inspecting the merged/conflicting
        state of 'Box<int>' after both dylibs' specializations have been
        pulled into the shared per-target scratch AST context.

        This intentionally evaluates expressions that reference each
        dylib's conflicting 'Box<int>' both separately and together, and
        dumps the AST/typesystem state at multiple points in between, to
        exercise the ASTImporter's template-instantiation-merging path
        (which is more fragile than plain class import) under a real ODR
        conflict. Whatever LLDB reports here (correct values, a
        gracefully-worded error, or a garbled/incomplete type) is fine;
        the important part is that none of these commands crash LLDB
        itself.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Import Left's 'Box<int>' (pad[1]) into the scratch AST context.
        # Use runCmd(check=False) throughout this test: as documented in
        # test_both_together above, once one dylib's conflicting
        # 'Box<int>' has been imported into the scratch AST context,
        # referring to the *other* dylib's conflicting 'Box<int>' in a
        # later/combined expression can fail with a graceful
        # "undeclared identifier" error instead of succeeding. That is a
        # known, acceptable outcome here -- the only thing this test
        # cares about is that none of these commands crash LLDB.
        self.runCmd("expression -- gLeftBox->pad[0]", check=False)
        # Import Right's 'Box<int>' (pad[4]) into the scratch AST context
        # as well. Depending on import order this may fail gracefully
        # (see test_both_together above) instead of succeeding.
        self.runCmd("expression -- gRightBox->pad[0]", check=False)
        # A combined expression that touches both conflicting
        # specializations in a single expression -- this is the point
        # where the ASTImporter has to reconcile (or fail to reconcile)
        # the two conflicting 'Box<int>' definitions.
        self.runCmd(
            "expression -- gLeftBox->pad[0] + gRightBox->pad[0]", check=False
        )

        # None of the following should crash LLDB, regardless of whether
        # the expressions above succeeded, failed gracefully, or produced
        # a garbled result. 'target modules dump ast --filter Box' (with
        # no module specified) dumps the per-module (DWARF-derived) AST
        # for 'Box' on *every* loaded module, i.e. the main executable
        # (which never saw a body for 'Box' at all) and both dylibs.
        self.expect("target modules dump ast --filter Box")
        self.expect("target dump typesystem")

        # Repeat once more after the dumps above to make sure inspecting
        # the (possibly corrupted) state didn't itself do anything worse.
        self.expect("target modules dump ast --filter Box")
        self.expect("target dump typesystem")
