"""
Test LLDB's behaviour when a class template ('Holder') is instantiated with
another class template ('Box') as one of its own template arguments (a
TemplateArgument of kind 'Template', not the far more common kind 'Type'),
and the class template used as that template-template argument has
conflicting, ODR-violating definitions across a main executable and a
dylib.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTemplateTemplateParamOdrTestCase(TestBase):
    def test_holder_alone_in_main_exe(self):
        """
        Looking at the main executable's 'Holder<Box, int>' (built by
        expanding main.cpp's definition of 'Box' through the
        template-template argument) on its own should work fine, before the
        dylib's conflicting definition of 'Box' is ever imported into the
        scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_holder.inner.val", result_type="int", result_value="111")
        self.expect_expr("main_holder.flag", result_type="int", result_value="1")

    def test_holder_alone_in_dylib(self):
        """
        Looking at the dylib's 'Holder<Box, int>' (built by expanding
        plugin.cpp's conflicting definition of 'Box', which has an extra
        leading 'tag' field) on its own should also work fine, before the
        main executable's conflicting definition of 'Box' is ever imported
        into the scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "plugin_holder.inner.val", result_type="int", result_value="222"
        )
        self.expect_expr("plugin_holder.flag", result_type="int", result_value="2")

    @expectedFailureAll(
        bugnumber="'Holder<Box, int>' is instantiated with a template "
        "template argument ('Box', a TemplateArgument of kind 'Template') "
        "whose underlying class template has conflicting, ODR-violating "
        "definitions in the main executable and a dylib; a single "
        "expression that references both modules' 'Holder<Box, int>' "
        "globals fails to resolve the second module's 'inner' field "
        "instead of producing a clean ODR diagnostic or correct merged "
        "value, and dumping either module's already-parsed Clang AST for "
        "'Holder' with 'target modules dump ast --filter' afterwards "
        "crashes LLDB outright (SIGSEGV in "
        "clang::RecursiveASTVisitor<...>::"
        "TraverseClassTemplateSpecializationDecl while the AST dumper's "
        "name filter traverses the 'Template' kind TemplateArgument, i.e. "
        "the nested 'Box' class-template-as-argument, of the already-"
        "parsed 'Holder<Box, int>' specialization)"
    )
    def test_holder_combined_expr_and_dump_ast_crashes(self):
        """
        Tests LLDB's behaviour when the same template-id 'Holder<Box, int>'
        is instantiated in both the main executable and a dylib, each
        module expanding the template-template argument 'Box' through its
        own, mutually conflicting definition of the 'Box' class template
        ('template<typename T> struct Box { T val; };' in main.cpp vs.
        'template<typename T> struct Box { int tag; T val; };' in
        plugin.cpp).

        First, evaluate an expression against each module's global on its
        own, forcing DWARFASTParserClang to parse both modules' (mutually
        conflicting) 'Box<int>' and 'Holder<Box, int>' from debug info into
        their respective per-module TypeSystemClang instances. Then combine
        both globals in a single expression, which forces the ASTImporter
        to import and reconcile the second module's 'Holder<Box, int>'
        into the target's shared scratch AST context, where the first
        module's conflicting 'Holder<Box, int>'/'Box<int>' already live.
        Because the mismatch here is hidden behind a template-template
        argument (a TemplateArgument of kind 'Template' referring to the
        'Box' ClassTemplateDecl, rather than a plain TemplateArgument of
        kind 'Type'), reconciling it requires the ASTImporter to compare
        and import the referenced ClassTemplateDecls themselves, and
        transitively their instantiated specializations - a much less
        exercised path than importing ordinary type template arguments.

        Finally, dump each module's parsed Clang AST for 'Holder' via
        'target modules dump ast --filter Holder'. Since 'Holder<Box, int>'
        has already been parsed into that module's TypeSystemClang by the
        expression evaluations above, this forces Clang's (name-filtered)
        RecursiveASTVisitor-based AST dumper to traverse the already-parsed
        'ClassTemplateSpecializationDecl' for 'Holder<Box, int>', including
        its 'Template' kind TemplateArgument for 'Box' - this is the
        traversal that segfaults LLDB outright.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # These alone should not crash LLDB, even though each one parses a
        # 'Holder<Box, int>'/'Box<int>' pair from one module's debug info
        # via the template-template argument 'Box'.
        self.expect_expr("main_holder.inner.val", result_type="int", result_value="111")
        self.expect_expr(
            "plugin_holder.inner.val", result_type="int", result_value="222"
        )

        # Combine both modules' conflicting 'Holder<Box, int>' globals in a
        # single expression, forcing the ASTImporter to reconcile the two
        # mutually conflicting definitions of 'Box' reached through the
        # template-template argument. This does not evaluate to the
        # correct merged sum (333) or a clean ODR diagnostic: instead the
        # second module's conflicting 'Holder<Box, int>' (already imported
        # into the scratch AST context by the first access above) loses
        # its 'inner' field entirely once reconciled against the first
        # module's conflicting 'Box'.
        self.expect(
            "expr main_holder.inner.val + plugin_holder.inner.val",
            error=True,
            substrs=["no member named 'inner' in 'Holder<Box, int>'"],
        )

        # With both modules' 'Holder<Box, int>' now parsed, dump each
        # module's Clang AST filtered on 'Holder'. This is the step that
        # crashes LLDB (see bugnumber above).
        self.expect("target modules dump ast --filter Holder")
