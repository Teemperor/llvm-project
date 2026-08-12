import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstVirtualVsNonvirtualDtorOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when a class 'Base' is polymorphic (has a
        virtual destructor and a virtual method, hence a vtable pointer)
        in the main executable, but the *same-named* class in a dylib has
        neither a virtual destructor nor a virtual method, and therefore
        has no vtable pointer at all. This is a deliberate ODR violation
        that doesn't just change field layout, it changes whether the
        class is a dynamic (polymorphic) class in the first place: the
        exe's 'Base' is 16 bytes (vtable pointer + int), while the
        dylib's 'Base' is 8 bytes (just the int, no vtable pointer).

        When both conflicting definitions of 'Base' end up referenced
        from the same debug session, LLDB's ASTImporter has to reconcile
        two CXXRecordDecls for 'Base' that disagree on isDynamicClass().
        A merged/confused CXXRecordDecl -- where the memoized "is this a
        dynamic class" bit doesn't match the DefinitionData that's
        actually being used for layout -- can send
        clang::ASTRecordLayoutBuilder and clang::ItaniumVTableBuilder
        down inconsistent paths when an expression tries to make a
        virtual call or compute sizeof/layout for the type.

        The exe's polymorphic 'Base' is kept alive via a
        std::unique_ptr<Base> and deleted polymorphically (through
        Base's virtual destructor) once both plugin_init() and
        plugin_entry() (which construct the dylib's non-polymorphic
        'Base') have run.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the dylib's (non-polymorphic, no-vtable) 'Base' first.
        self.expect_expr("b", result_type="Base")
        self.expect("expression sizeof(Base)")

        # Dump the dylib's per-module AST for 'Base' before the exe's
        # conflicting definition has been imported into the shared
        # scratch AST context.
        self.expect("target modules dump ast --filter Base")

        bkpt = self.target().BreakpointCreateBySourceRegex(
            "b->f\\(\\);", lldb.SBFileSpec("main.cpp")
        )
        self.assertTrue(bkpt.GetNumLocations() > 0, "Set breakpoint in main.cpp")

        self.runCmd("continue")
        self.assertState(self.process().GetState(), lldb.eStateStopped)

        # Now reference the exe's polymorphic, vtable-having 'Base'
        # (through the std::unique_ptr<Base> that owns it). This pulls in
        # the exe's CXXRecordDecl for 'Base' -- and a bunch of libc++
        # template instantiations that reference it (default_delete<Base>,
        # unique_ptr<Base>::~unique_ptr(), etc.) -- into the same shared
        # scratch AST context that already has the dylib's conflicting,
        # non-polymorphic 'Base' imported into it.
        self.expect("expression sizeof(Base)")

        # Dump the per-module AST for 'Base' again, and the merged scratch
        # AST context, now that both conflicting definitions of 'Base'
        # have been referenced in the same debug session.
        self.expect("target modules dump ast --filter Base")
        self.expect("target dump typesystem")

        # Finally, exercise a virtual call/destroy through the exe's
        # (supposedly) polymorphic 'Base' while the dylib's conflicting,
        # non-polymorphic definition is also alive in the AST context.
        # This is the path that would make ItaniumVTableBuilder /
        # ItaniumCXXABI::EmitVirtualDestructorCall index into a vtable
        # layout that doesn't actually match the CXXRecordDecl being used,
        # if the ODR conflict above corrupted the merged type.
        self.expect("expression b->f()")
