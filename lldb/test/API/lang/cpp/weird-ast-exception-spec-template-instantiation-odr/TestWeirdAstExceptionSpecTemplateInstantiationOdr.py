"""
Test LLDB's behaviour when a dependent template specialization
('Wrapper<int>') is defined identically-by-name in a main executable and
a dylib -- same qualified name, same field layout, and the same mangled
linkage name for its 'release' method -- but the two definitions'
dependent noexcept-specifications disagree once actually evaluated for
'T = int': the exe's 'release' is noexcept(true)
('noexcept(sizeof(T) <= 4)'), while the dylib's is noexcept(false)
('noexcept(sizeof(T) <= 4 && sizeof(int) > 100)', an always-false extra
conjunct that isn't obviously different from a purely name-based
comparison of the specialization).

Clang only actually computes and caches a dependent noexcept-expression's
truth value lazily, via Sema::ResolveExceptionSpec, typically the first
time the function is called or 'noexcept(expr)' is evaluated on it. Since
LLDB's DWARFASTParserClang/ASTImporter machinery fabricates the two
conflicting ClassTemplateSpecializationDecls for 'Wrapper<int>'
independently -- one per module's DWARF -- and only reconciles them
later (if at all) inside the target's shared scratch ASTContext, this
tries to provoke that reconciliation into producing more than just a
wrong answer: a stale/mismatched cached FunctionProtoType, or a type
identity cycle/duplicate-insertion assertion in
ASTContext::getFunctionType's FoldingSet (which folds the exception spec
into the function type's identity).
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstExceptionSpecTemplateInstantiationOdrTestCase(TestBase):
    def test(self):
        """
        Evaluates expressions that call 'release()' through each
        module's conflicting 'Wrapper<int>' individually, then combines
        both in a single expression (forcing LLDB to reconcile the two
        same-named, same-layout, differently-noexcept
        ClassTemplateSpecializationDecls within one expression's AST),
        assigns one module's global into the other's (forcing a real
        ASTImporter merge into the shared scratch AST context), and
        dumps both the per-module ASTs and the shared scratch
        TypeSystem/ASTContext at several points in between -- to check
        that none of this corrupts LLDB's internal Clang AST state badly
        enough to crash it outright (as opposed to merely giving an
        imprecise or wrong-but-well-formed answer).
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the exe's 'Wrapper<int>' first, calling its
        # noexcept(true) 'release' and pulling its definition into
        # LLDB's per-module AST.
        self.expect_expr("g_main_wrapper.val", result_type="int", result_value="111")
        self.expect("expression g_main_wrapper.release()")

        # Now also reference the dylib's conflicting, differently
        # (noexcept(false)) 'Wrapper<int>::release', from the same debug
        # session.
        self.expect_expr(
            "g_plugin_wrapper.val", result_type="int", result_value="222"
        )
        self.expect("expression g_plugin_wrapper.release()")

        # Dump the per-module ASTs and the shared scratch AST context
        # while both conflicting definitions are alive, to poke at any
        # state that got merged/reconciled across the two.
        self.expect("target modules dump ast --filter Wrapper")
        self.expect("target dump typesystem")

        # Combine both modules' conflicting 'Wrapper<int>' globals in a
        # single expression. Since both specializations share the exact
        # same field layout ('int val;'), this succeeds and forces LLDB
        # to reconcile the alias-free, DWARF-imported 'Wrapper<int>' from
        # each module within the very same expression's AST context.
        self.expect_expr(
            "g_main_wrapper.val + g_plugin_wrapper.val",
            result_type="int",
            result_value="333",
        )

        # Assign one module's global into the other's. This is a
        # same-layout, same-mangled-name assignment between the two
        # conflicting definitions, which forces a real ASTImporter merge
        # of one module's 'Wrapper<int>' RecordDecl into the target's
        # shared scratch ASTContext (as opposed to each module's AST
        # staying independent, which is what happens when each global is
        # only ever referenced on its own).
        self.expect("expression g_main_wrapper = g_plugin_wrapper")
        self.expect("expression g_main_wrapper.release()")
        self.expect("expression g_plugin_wrapper = g_main_wrapper")
        self.expect("expression g_plugin_wrapper.release()")

        # One last set of dumps after all of the above, in case
        # corruption only shows up once several conflicting operations
        # have run.
        self.expect("target modules dump ast --filter Wrapper")
        self.expect("target dump typesystem")
