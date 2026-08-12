import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstVtableOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when a polymorphic class ('Widget') is
        defined with conflicting vtable shapes in the main executable and
        in a dylib: the exe's 'Widget' has two virtual methods (f and g)
        and no data members, while the dylib's 'Widget' has only one
        virtual method (f) plus an extra non-virtual int field. This is
        an ODR violation that gives the two same-named CXXRecordDecls
        different vtable layouts *and* different object sizes.

        When an expression references both conflicting definitions of
        'Widget' at once, LLDB's ASTImporter has to merge/reconcile the
        two CXXRecordDecls in the shared scratch AST context. Computing a
        complete record layout (e.g. for sizeof, member access, or
        printing the object) for such a merged/conflicting type can
        confuse Clang's ASTRecordLayoutBuilder or ItaniumVTableBuilder,
        which assume a single, consistent definition of the class. This
        test deliberately avoids calling any virtual method (which would
        require JIT-calling into the inferior) and instead focuses purely
        on type-system/layout operations: sizeof, field access, and
        formatting/printing the object.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the exe's 'Widget' global first, so its (2-vtable-slot)
        # definition is the one that ends up in LLDB's scratch AST context.
        self.expect_expr("global_widget")

        # Now also reference the dylib's conflicting 'Widget' global (with
        # its 1-vtable-slot definition and extra 'extra' field) from the
        # same debug session. This forces the ASTImporter to reconcile the
        # two conflicting CXXRecordDecls for 'Widget'.
        self.expect_expr("gPluginWidget->extra", result_type="int", result_value="42")

        # Print the object itself, which requires computing a complete
        # record layout for the (potentially merged/conflicting) type.
        self.expect("expression *gPluginWidget")

        # Take the size of both conflicting definitions in a single
        # expression. The two 'Widget' types have different sizes because
        # of the different vtable shape and extra field, so this exercises
        # ASTRecordLayoutBuilder for both definitions back-to-back.
        self.expect("expression sizeof(global_widget) + sizeof(*gPluginWidget)")

        # Access a field through the dylib's definition while the exe's
        # conflicting definition is also alive in the AST context.
        self.expect("expression gPluginWidget->extra + 1")
