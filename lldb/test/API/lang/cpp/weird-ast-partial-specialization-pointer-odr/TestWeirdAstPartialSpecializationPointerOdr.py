"""
Test LLDB's behaviour when the exact same class template specialization
type-id ('Container<int *>') is instantiated from a pointer partial
specialization in one module (the main executable) and from the plain
primary template in another module (a dylib), because the dylib never
declares the partial specialization at all.

Both modules' 'Container<int *>' share the same name, the same mangled
name and the same template argument list ('int *'), but are structurally
unrelated in Clang's AST: main.cpp's version is a
ClassTemplateSpecializationDecl instantiated from a
ClassTemplatePartialSpecializationDecl (three fields: 'val', 'meta',
'extra'), while plugin.cpp's version is a ClassTemplateSpecializationDecl
instantiated from the plain primary ClassTemplateDecl (two fields: 'val',
'meta'). This is a case the ASTImporter's ODR-checking/merging logic may
not expect: it is normally built around two implicit instantiations of
the *same* template pattern disagreeing on layout, not one side being
instantiated from a partial specialization that the other side's module
never even declares.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstPartialSpecializationPointerOdrTestCase(TestBase):
    def test_main_container_alone(self):
        """
        Looking at the main executable's 'Container<int *>' (instantiated
        from the pointer partial specialization, with 'val'/'meta'/'extra'
        members) on its own should work fine, before the dylib's
        conflicting primary-template instantiation of the same type-id is
        ever imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_container.val", result_type="int *")
        self.expect_expr("main_container.meta", result_type="int", result_value="222")
        self.expect_expr("main_container.extra", result_type="long", result_value="333")

    def test_plugin_container_alone(self):
        """
        Looking at the dylib's 'Container<int *>' (instantiated from the
        plain primary template, with only 'val'/'meta' members) on its
        own should work fine, before the main executable's conflicting
        partial-specialization instantiation of the same type-id is ever
        imported into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("plugin_container.val", result_type="int *")
        self.expect_expr(
            "plugin_container.meta", result_type="int", result_value="555"
        )

    @expectedFailureAll(
        bugnumber="ASTImporter can't reconcile a class template specialization "
        "instantiated from a pointer partial specialization in one module "
        "against a same-named, same-mangled, same-template-argument "
        "specialization instantiated from the plain primary template (no "
        "partial specialization declared at all) in another module: "
        "whichever module's 'Container<int *>' is imported into the scratch "
        "AST context first wins, and referring to the other module's global "
        "of that same specialization afterwards fails to evaluate"
    )
    def test_both_together(self):
        """
        Reference both modules' conflicting 'Container<int *>' instances
        together. This forces the ASTImporter to import and reconcile
        main.cpp's partial-specialization-derived 'Container<int *>'
        against plugin.cpp's primary-template-derived 'Container<int *>'
        within the same scratch AST context.

        Using both conflicting definitions in the same expression should
        not crash LLDB, but currently whichever definition is imported
        first "wins": subsequent references to the other module's global
        of the same specialization fail (either with "use of undeclared
        identifier" or, if a pointer to it is taken after the first
        definition has already been imported as complete, with
        "Couldn't materialize: invalid type: cannot determine size").
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "main_container.meta + plugin_container.meta",
            result_type="int",
            result_value="777",
        )
