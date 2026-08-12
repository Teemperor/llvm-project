import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstNoexceptVirtualDtorOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when a polymorphic class hierarchy
        ('Base'/'Derived') is defined with conflicting exception
        specifications on its virtual member functions in the main
        executable versus in a dylib: the exe's 'Base' has a noexcept
        virtual destructor and a plain (not noexcept) virtual 'f', while
        the dylib's same-named 'Base' has the noexcept-ness of those two
        methods swapped (plain destructor, noexcept 'f'). 'Derived'
        overrides the destructor in both definitions. This is an ODR
        violation that gives the two same-named CXXRecordDecls virtual
        member functions occupying the very same vtable slots but with
        mismatched FunctionProtoType exception specifications -- exactly
        the kind of state that Sema::CheckOverridingFunctionExceptionSpec
        would normally reject at parse time, but that LLDB's
        DWARFASTParserClang/ASTImporter can end up fabricating post-hoc
        from two independent DWARF definitions.

        This exercises: evaluating expressions that reference both
        conflicting definitions in the same expression, calling the
        (JIT-dispatched) virtual method and destructor through each
        conflicting definition, and dumping both the per-module ASTs and
        the shared per-target scratch AST context in between, to check
        that none of this corrupts LLDB's internal Clang AST state badly
        enough to crash it (as opposed to merely giving an imprecise or
        wrong-but-well-formed answer).
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the exe's 'Derived' global first, so its definition
        # (noexcept dtor, non-noexcept 'f') ends up in LLDB's scratch AST
        # context.
        self.expect("expression global_derived")

        # Now also reference the dylib's conflicting 'Derived'/'Base'
        # globals (with the noexcept-ness of the dtor and 'f' swapped)
        # from the same debug session. This forces LLDB's type-system
        # machinery to reconcile the two conflicting CXXRecordDecls.
        self.expect("expression *gPluginDerived")

        # Dump the per-module ASTs and the shared scratch AST context
        # while both conflicting definitions are alive, to poke at any
        # state that got merged/reconciled across the two.
        self.expect("target modules dump ast --filter Base")
        self.expect("target modules dump ast --filter Derived")
        self.expect("target dump typesystem")

        # Call the virtual method through both conflicting definitions
        # in a single expression. Calling a virtual method requires
        # LLDB's expression evaluator to JIT-call into the inferior via
        # the (possibly-merged) vtable, exercising ItaniumVTableBuilder /
        # VTableContext for both definitions back-to-back.
        self.expect("expression global_derived.f(); gPluginBase->f();")

        # Explicitly invoke the (virtual, differently-noexcept-marked)
        # destructor through each conflicting definition too.
        self.expect(
            "expression Derived d; d.f(); Base &br = d; br.~Base();",
        )
        self.expect("expression gPluginDerived->~Derived();")

        # One last dump after all of the above, in case corruption only
        # shows up once several conflicting operations have run.
        self.expect("target dump typesystem")
