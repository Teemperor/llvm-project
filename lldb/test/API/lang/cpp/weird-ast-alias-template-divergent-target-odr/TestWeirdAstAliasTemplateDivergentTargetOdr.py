"""
Test LLDB's behaviour when a class template specialization ('Wrapper<int>')
has two conflicting definitions across a main executable and a dylib, and
both modules access it exclusively through an identically-spelled alias
template ('Ptr<int>', i.e. 'Wrapper<int> *') rather than by naming
'Wrapper<int>' directly.

Alias templates are pure sugar: DWARF may or may not preserve them, so
when LLDB's expression parser sees 'Ptr<int>' it has to independently
re-derive the underlying type ('Wrapper<int> *') via TypeSystemClang,
while the ASTImporter is (elsewhere) importing a conflicting
'Wrapper<int>' RecordDecl from DWARF for the other module. This test
combines both modules' 'Ptr<int>' globals in a single expression to see
whether the alias-substituted 'Wrapper<int>' and the DWARF-imported
'Wrapper<int>' end up as two different, incompatible QualTypes colliding
inside the same expression.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstAliasTemplateDivergentTargetOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each module's 'Ptr<int>' global can be dereferenced on its own,
        before the other module's conflicting definition of 'Wrapper<int>'
        has been imported into the target's shared scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("main_ptr->v", result_type="int", result_value="111")
        self.expect_expr("plugin_ptr->v", result_type="int", result_value="333")

    @expectedFailureAll(
        bugnumber="two conflicting definitions of the same class template "
        "specialization ('Wrapper<int>'), reached exclusively through an "
        "identically-spelled alias template ('Ptr<int>') in a main "
        "executable and a dylib, confuse LLDB's name lookup once both "
        "have been evaluated individually: combining 'main_ptr->v' and "
        "'plugin_ptr->v' (in either order) in a single expression fails "
        "to find whichever of the two global variables is referenced "
        "second, with a spurious 'use of undeclared identifier' error, "
        "even though each name resolves fine on its own"
    )
    def test_combine_both_in_one_expression(self):
        """
        Tests LLDB's behaviour when the exact same class template
        specialization ('Wrapper<int>') has two incompatible definitions -
        one in the main executable ('Wrapper<T> { T v; long extra; }') and
        a conflicting one in a dylib ('Wrapper<T> { T v; short tag; }') -
        both reached exclusively through an identically-spelled alias
        template 'Ptr<int>' ('using Ptr = Wrapper<T> *').

        Referencing both modules' 'Ptr<int>' globals ('main_ptr' and
        'plugin_ptr') in a single expression forces LLDB to reconcile the
        alias-substituted 'Wrapper<int>' against the DWARF-imported,
        conflicting 'Wrapper<int>' from the other module within the same
        expression's AST context.

        NOTE ON A REAL CRASH FOUND WHILE EXPLORING THIS SCENARIO: this
        test deliberately does NOT exercise it, since a genuine crash
        would take down the whole test process instead of failing
        cleanly. After evaluating 'main_ptr->v' and then 'plugin_ptr->v'
        individually (each of which succeeds on its own - see
        test_each_alone above), running
            target modules dump ast --filter Ptr
        segfaults LLDB outright, with the crashing thread's top frames in
        clang::RecursiveASTVisitor<ASTPrinter>::
        TraverseClassTemplateSpecializationDecl and
        (anonymous namespace)::ASTPrinter::TraverseDecl, called from
        TypeSystemClang::Dump. The same '--filter Wrapper' (matching the
        record name directly instead of the alias) does not reproduce it,
        nor does dumping without any filter, nor does
        'target dump typesystem'; it is specifically filtering the AST
        dump by the alias template's name ('Ptr') after both conflicting
        'Wrapper<int>' specializations have been individually imported
        into the scratch context that triggers the segfault.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Individually, both resolve fine (and bring their respective,
        # conflicting 'Wrapper<int>' into the scratch AST context).
        self.expect_expr("main_ptr->v", result_type="int", result_value="111")
        self.expect_expr("plugin_ptr->v", result_type="int", result_value="333")

        # Combining both in one expression is where things go wrong: this
        # is expected to fail to resolve one of the two identifiers.
        self.expect_expr("main_ptr->v + plugin_ptr->v", result_type="int", result_value="444")
