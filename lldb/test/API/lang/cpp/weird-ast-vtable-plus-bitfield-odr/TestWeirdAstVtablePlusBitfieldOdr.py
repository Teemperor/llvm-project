import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstVtablePlusBitfieldOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when the same class name ('Combo') is
        defined with TWO stacked ODR conflicts at once between the main
        executable and a dylib: a different vtable shape (the exe's
        'Combo' has one virtual method, the dylib's has two) *and* a
        different bitfield layout (the exe's 'Combo' has two narrow 5-bit
        bitfields 'x'/'y', the dylib's has a single wide 20-bit bitfield
        'x' and no 'y').

        Combining both conflict dimensions in the very same RecordDecl
        maximizes the chance that when LLDB's ASTImporter
        merges/reconciles the two conflicting CXXRecordDecls for 'Combo'
        in the shared scratch AST context, at least one of Clang's
        several interacting layout subsystems - the ItaniumVTableBuilder
        (which assumes a single consistent set of virtual methods) and
        the RecordLayoutBuilder's bitfield packer (which assumes a
        single consistent set of bitfields) - trips over the resulting
        Frankenstein decl, rather than just producing a wrong answer.

        This test deliberately avoids calling any virtual method (which
        would require JIT-calling into the inferior) and instead focuses
        purely on type-system/layout operations: sizeof and bitfield
        member reads/writes, combining globals from both modules in the
        same expressions.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the exe's 'Combo' global first, so its definition
        # (1 vtable slot, 5-bit 'x'/'y' bitfields) is the one that ends
        # up in LLDB's scratch AST context.
        self.expect_expr(
            "global_combo.x", result_type="unsigned int", result_value="3"
        )
        self.expect_expr(
            "global_combo.y", result_type="unsigned int", result_value="2"
        )

        # Now also reference the dylib's conflicting 'Combo' global (2
        # vtable slots, single wide 20-bit 'x' bitfield, no 'y') from the
        # same debug session. This forces the ASTImporter to reconcile
        # the two conflicting CXXRecordDecls for 'Combo', combining the
        # vtable-shape conflict and the bitfield-layout conflict in one
        # merge.
        self.expect_expr(
            "gPluginCombo->x", result_type="unsigned int", result_value="123456"
        )

        # Combine reads of the conflicting bitfield members from both
        # modules in a single expression, forcing both conflicting
        # 'Combo' layouts to be alive in the AST context together.
        self.expect_expr("global_combo.x + gPluginCombo->x")

        # Take the size of both conflicting definitions in a single
        # expression. The two 'Combo' types have differently shaped
        # vtables and differently packed bitfields (even though both
        # happen to round up to the same overall byte size on this
        # target), so this exercises ASTRecordLayoutBuilder (and its
        # vtable-pointer and bitfield-bucket logic) for both definitions
        # back-to-back.
        self.expect("expression sizeof(global_combo) + sizeof(*gPluginCombo)")

        # Write through a bitfield member of one module's definition
        # while the other module's conflicting definition is also alive
        # in the AST context.
        self.expect("expression global_combo.y = 5")
        self.expect_expr(
            "global_combo.y", result_type="unsigned int", result_value="5"
        )

        # Print the objects themselves, which requires computing a
        # complete record layout (vtable pointer + bitfield bucket) for
        # the (potentially merged/conflicting) type.
        self.expect("expression global_combo")
        self.expect("expression *gPluginCombo")
