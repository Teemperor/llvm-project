import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTypedefAnonStructOdrTestCase(TestBase):
    def test(self):
        """
        Puts LLDB's ASTImporter into a weird state using the classic
        C-style "typedef struct { ... } Foo;" idiom (which also compiles
        fine in C++). Both the main executable and a dylib define their own
        anonymous (tag-less) struct and alias it to the same typedef name
        "Foo", but the two anonymous structs disagree on their members
        (main.cpp: {int a;}, plugin.cpp: {int a; int b; long c;}), and
        therefore on size.

        Because the underlying RecordDecls are unnamed, they cannot be
        matched via the usual named-tag ODR/redeclaration logic that keys
        off a DeclarationName; the only surviving name is the TypedefDecl
        "Foo" that happens to point at each of them. When the expression
        evaluator needs a single 'Foo' type spanning both the main
        executable and the dylib, the ASTImporter has to reconcile two
        typedefs of the same name whose underlying anonymous RecordDecls
        are structurally different. This exercises an ODR-conflict path
        that is less battle-tested than the named-tag case, and this test
        doesn't assert a specific outcome for the ambiguous parts; it
        primarily makes sure LLDB survives evaluating these expressions
        instead of crashing.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Access members through both globals via their own module's view
        # of 'Foo'.
        self.expect_expr("main_foo.a", result_value="1")
        self.expect_expr("plugin_foo.a", result_value="2")

        # Combine the two conflicting 'Foo' definitions in one expression
        # to force LLDB to reconcile both typedefs (and their anonymous
        # RecordDecls) into the same AST context.
        self.expect_expr("main_foo.a + plugin_foo.a", result_value="3")

        # sizeof(Foo) is ambiguous between the two definitions; just make
        # sure evaluating it doesn't crash LLDB.
        self.expect("expression sizeof(Foo)")

        # Access a member that only exists in the dylib's (larger)
        # definition of the anonymous struct behind 'Foo'.
        self.expect("expression plugin_foo.c")

        # Look up the typedef itself; this forces LLDB to resolve 'Foo' to
        # a concrete type instead of just evaluating an expression.
        self.expect("image lookup -t Foo")
