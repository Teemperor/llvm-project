"""
Test LLDB's handling of an ODR violation where the same unqualified struct
name 'Helper' is defined with two different linkages in two different
modules: internal linkage (wrapped in an anonymous namespace) in the main
executable, and external linkage (ordinary namespace scope) in a dylib.

This forces TypeSystemClang/ASTImporter to decide whether the
internal-linkage 'Helper' and the external-linkage 'Helper' should be
treated as "the same" type when both get pulled into the per-target shared
scratch AST context, since ASTImporter's structural-equivalence checks
don't consistently take Decl::getLinkageInternal() into account before
merging same-named RecordDecls that originate from different
DWARFASTParserClang/SymbolFileDWARF modules.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstInternalVsExternalLinkageOdrTestCase(TestBase):
    def test_qualified_access_each_side(self):
        """
        Each global can be read and have its (identically-named, but
        differently-linked) 'run()' method called just fine as long as we
        always go through the global variable itself (never construct a
        bare, unqualified 'Helper' value) -- this doesn't require LLDB to
        decide whether the two 'Helper's are "the same" type.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Dump the per-module ASTs for the conflicting 'Helper' type. This
        # forces both the internal-linkage (anonymous namespace) 'Helper'
        # from the main executable and the external-linkage 'Helper' from
        # the dylib to be parsed via DWARFASTParserClang.
        self.expect(
            "target modules dump ast --filter Helper",
            substrs=["Helper"],
        )

        self.expect_expr("gHelper.tag", result_type="int", result_value="1")
        self.expect_expr("gPluginHelper.tag", result_type="int", result_value="2")

        # Calling .run() on each requires LLDB to mangle a call to
        # 'Helper::run' via the JIT for both the internal-linkage
        # definition (mangled as '_ZN12_GLOBAL__N_16Helper3runEv') and the
        # external-linkage one (mangled as '_ZN6Helper3runEv'). Do this
        # after having dumped/parsed both ASTs above, since that's the
        # point where any merged/corrupted scratch AST state would matter.
        self.expect_expr(
            "gHelper.run(), gPluginHelper.run(), gHelper.tag + gPluginHelper.tag",
            result_type="int",
            result_value="300",
        )

        # This should not have corrupted the shared scratch AST context.
        self.expect(
            "target dump typesystem",
            substrs=["State of scratch Clang type system"],
        )

    @expectedFailureAll(
        bugnumber="ASTImporter/TypeSystemClang don't check Decl::getLinkageInternal() "
        "before treating two same-named RecordDecls from different modules as "
        "redeclarations of 'the same' type: once one module's 'Helper' (here, the "
        "internal-linkage one from the main executable) has been imported into the "
        "scratch AST as the unqualified name 'Helper', the other module's "
        "external-linkage 'Helper' becomes permanently unreachable by its own name "
        "in expressions, and assigning between values of the two "
        "structurally-identical-looking 'Helper's fails with a spurious "
        "'no viable conversion' error"
    )
    def test_unqualified_helper_ambiguity(self):
        """
        Tests the actual ODR ambiguity: constructing a bare, unqualified
        'Helper' value forces TypeSystemClang to pick one of the two
        conflicting (internal-linkage vs external-linkage) definitions,
        and once picked, the *other* module's 'Helper' can no longer be
        named or converted to/from via the unqualified spelling -- even
        though both structs are spelled identically ('struct Helper { int
        tag; void run(); };') modulo linkage.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Construct+call through a bare, unqualified 'Helper' -- this
        # binds to whichever module's 'Helper' definition LLDB's
        # unqualified-name lookup resolves to first.
        self.expect_expr("Helper{}.run(), Helper{1}.tag", result_type="int", result_value="1")

        # Import the internal-linkage 'Helper' (from the main executable)
        # into the scratch AST as a persistent variable.
        self.expect("expr Helper $h = gHelper", error=False)

        # Now try to import the dylib's external-linkage 'Helper' the same
        # way. Ideally this should also succeed (they're two distinct,
        # unrelated types that both happen to be spelled 'Helper'), but
        # because the unqualified name 'Helper' was already bound to the
        # main executable's internal-linkage definition in the scratch
        # AST, this instead fails with a "no viable conversion" diagnostic
        # complaining that 'Helper' (the dylib's) can't convert to
        # 'Helper' (aka '(anonymous namespace)::Helper', the main
        # executable's).
        self.expect(
            "expr Helper $h2 = gPluginHelper",
            error=False,
            substrs=["$h2"],
        )
