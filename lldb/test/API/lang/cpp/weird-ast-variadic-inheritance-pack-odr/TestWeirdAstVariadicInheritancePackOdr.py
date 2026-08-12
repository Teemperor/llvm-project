"""
Test LLDB's behaviour with a variadic class template ('Multi<Ts...>') that
inherits from 'Mixin<Ts>...' via a pack expansion in the base-specifier
list, where the main executable and a dylib both define a same-named
alias 'M' for a 'Multi<...>' instantiation, but instantiate the pack in
opposite orders ('Multi<int, char>' vs. 'Multi<char, int>').

Pack-expanded base-specifiers give the CXXRecordDecl a variable-length
array of CXXBaseSpecifiers built by expanding a TemplateArgument pack,
rather than a hand-written base list. Swapping the pack order changes the
*order* (though not the *set*) of 'Multi's bases, and therefore the
offsets of the inherited 'Mixin<T>::tag' members and of 'Multi::id' -- all
while both modules report "the same" DWARF-visible typedef name 'M'. This
exercises the TemplateArgument-pack-to-CXXBaseSpecifier expansion
machinery instead of a hand-written multiple-inheritance base list.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstVariadicInheritancePackOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each module's 'M' (an alias for a differently-ordered 'Multi<...>'
        pack instantiation) can be read on its own, before the other
        module's conflicting instantiation order has been imported into
        the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("sizeof(M)", result_value="12")
        self.expect_expr("global_m.id", result_type="int", result_value="1")
        self.expect_expr(
            "global_m.Mixin<int>::tag", result_type="int", result_value="2"
        )
        self.expect_expr(
            "global_m.Mixin<char>::tag", result_type="char", result_value="'A'"
        )

    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same alias name 'M' has two
        incompatible definitions across the executable and a dylib: main
        defines 'using M = Multi<int, char>;' (base order Mixin<int>,
        then Mixin<char>), while the dylib defines 'using M =
        Multi<char, int>;' for the SAME alias name (base order
        Mixin<char>, then Mixin<int>). Both modules' 'M' have the same
        *set* of base classes but a different *order*, which can affect
        the base-class layout (and therefore the offsets of 'id' and of
        each Mixin<T>::tag) even though both are DWARF-visible under the
        identical typedef name 'M'.

        Reading both modules' globals of type 'M' in the same sequence of
        expressions forces LLDB to import and reconcile both conflicting
        pack-expansion-derived base-specifier orders within the same
        scratch AST context. This evaluates fine and returns the correct,
        module-specific values without crashing or corrupting either
        definition.

        A further, more aggressive way to poke at this same ODR conflict
        - which is deliberately NOT exercised by this test since it
        segfaults the whole LLDB process rather than failing cleanly -
        is running 'target modules dump ast --filter Multi' (or
        '--filter Mixin') after evaluating expressions like these. That
        crashes LLDB via a null/invalid pointer dereference inside
        clang::RecursiveASTVisitor::TraverseClassTemplateSpecializationDecl,
        reached from TypeSystemClang::Dump()'s ASTPrinter while traversing
        the scratch TranslationUnitDecl. The crash reproduces even
        without any ODR conflict at all (a single module defining a
        struct that inherits from one or more class-template
        specializations, e.g. 'struct Multi : Mixin<int>, Mixin<char> {};',
        is enough, once an expression has imported 'Multi' into the
        scratch AST context and 'target modules dump ast --filter <name>'
        is then run) -- the ODR conflict in this test just additionally
        forces the ASTImporter to reconcile two differently-ordered
        pack-expansion base-specifier lists before that (still-crashing)
        AST dump would be reached.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference the main executable's 'M' first, so its 'Multi<int,
        # char>' instantiation (Mixin<int> then Mixin<char> base order)
        # ends up in the scratch AST context.
        self.expect_expr("sizeof(M)", result_value="12")
        self.expect_expr("global_m.id", result_type="int", result_value="1")

        # Now bring in the dylib's conflicting 'M' ('Multi<char, int>',
        # opposite base order) via the same alias name, forcing the
        # ASTImporter to reconcile both pack-expansion-derived
        # CXXBaseSpecifier lists at once.
        self.expect_expr("sizeof(*gPluginM)", result_value="12")
        self.expect_expr("gPluginM->id", result_type="int", result_value="10")
        self.expect_expr(
            "gPluginM->Mixin<char>::tag", result_type="char", result_value="'B'"
        )
        self.expect_expr(
            "gPluginM->Mixin<int>::tag", result_type="int", result_value="30"
        )

        # Combine both modules' conflicting 'M' objects in a single
        # expression.
        self.expect_expr(
            "global_m.id + gPluginM->id",
            result_type="int",
            result_value="11",
        )

        # Finally, print whole objects (forces complete record layout,
        # including all pack-expanded base-class subobjects, for both
        # conflicting orderings of 'M') in the same expression.
        self.expect("expression global_m")
        self.expect("expression *gPluginM")
        self.expect_expr(
            "sizeof(global_m) + sizeof(*gPluginM)",
            result_type="unsigned long",
            result_value="24",
        )
