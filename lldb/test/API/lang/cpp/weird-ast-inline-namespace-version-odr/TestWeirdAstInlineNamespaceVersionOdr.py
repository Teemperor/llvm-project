import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstInlineNamespaceVersionOdrTestCase(TestBase):
    def test_qualified_lookup_through_inline_namespace(self):
        """
        Baseline: even without any ODR conflict, LLDB's expression parser
        fails to find a type via a qualified name that is only reachable by
        looking transparently through an inline namespace (e.g. 'lib::Handle'
        when 'Handle' is actually declared in 'lib::v1' and 'v1' is an inline
        namespace). This reproduces regardless of the ODR violation in
        plugin.cpp: it is a pre-existing limitation of qualified type-name
        lookup for inline namespaces in the expression evaluator, even though
        DWARFASTParserClang correctly marks the imported NamespaceDecl as
        'inline' (as shown by 'target modules dump ast'), and even though
        unqualified/variable-based lookup (e.g. evaluating a variable of that
        type) works fine.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "return local.id", lldb.SBFileSpec("main.cpp")
        )

        # Variable-based lookup finds the type fine.
        self.expect_expr(
            "hA",
            result_type="lib::Handle",
            result_children=[ValueCheck(name="id", value="1")],
        )

        # But looking up the type by its transparent qualified name fails.
        self.expect(
            "expr lib::Handle{}.id",
            error=True,
            substrs=["no member named 'Handle' in namespace 'lib'"],
        )

        # The fully-qualified (non-transparent) name does work.
        self.expect_expr("lib::v1::Handle{}.id", result_type="int", result_value="0")

    @expectedFailureAll(
        bugnumber="LLDB's expression parser cannot find a type via a "
        "qualified name that is only reachable by transparently looking "
        "through an inline namespace ('lib::Handle' where 'Handle' lives in "
        "the inline namespace 'lib::v1'), so this legitimate C++ syntax "
        "(which a real compiler accepts) is rejected with 'no member named "
        "Handle in namespace lib'"
    )
    def test_qualified_lookup_through_inline_namespace_should_work(self):
        """
        Same setup as test_qualified_lookup_through_inline_namespace, but
        documenting what *should* happen per the C++ standard: 'lib::Handle'
        is valid C++ (inline namespaces are transparent to name lookup) and
        should evaluate the same as 'lib::v1::Handle'.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "return local.id", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("lib::Handle{}.id", result_type="int", result_value="0")

    def test_conflicting_handle_both_imported(self):
        """
        Import BOTH conflicting definitions of 'lib::Handle' (one from the
        main executable via inline namespace 'v1', one from the dylib via
        inline namespace 'v2') into the per-target shared scratch
        AST context by evaluating expressions that reference each of them
        while stopped in frames belonging to their respective TUs, then
        inspect the merged scratch AST and dump the per-module ASTs.

        This should not crash LLDB. In particular this asserts that the
        ASTImporter keeps the two 'Handle' RecordDecls distinct (since they
        live in genuinely distinct NamespaceDecls for 'v1' and 'v2', even
        though both are transparently reachable as 'lib::Handle'), rather
        than incorrectly merging them into a single conflicting RecordDecl
        that could cause an out-of-bounds field access later.
        """
        self.build()

        target, process, main_thread, main_bkpt = lldbutil.run_to_source_breakpoint(
            self, "plugin_init();", lldb.SBFileSpec("main.cpp")
        )

        # Import main.cpp's 'lib::Handle' (4 bytes: { int id; }) into the
        # scratch AST context while stopped in a.out's frame.
        self.expect_expr("hA", result_type="lib::Handle")

        # Now switch to a frame inside the dylib's TU (plugin_entry) and
        # import the dylib's conflicting 'lib::Handle' (16 bytes:
        # { void *ptr; long tag; }) into the SAME scratch AST context.
        lldbutil.continue_to_source_breakpoint(
            self, process, "(void)local;", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "hB",
            result_type="lib::Handle",
            result_children=[
                ValueCheck(name="ptr"),
                ValueCheck(name="tag", value="2"),
            ],
        )

        # Dump the merged scratch AST: both 'lib::v1::Handle' and
        # 'lib::v2::Handle' should be present as two *distinct* RecordDecls
        # rather than a single corrupted/merged one.
        self.expect(
            "target dump typesystem",
            substrs=[
                "NamespaceDecl",
                "lib",
                "v1 inline",
                "v2 inline",
            ],
        )
        self.expect(
            "target dump typesystem",
            substrs=["ptr 'void *'", "tag 'long'"],
        )
        self.expect(
            "target dump typesystem",
            substrs=["id 'int'"],
        )

        # Re-evaluating both should still be well-formed and should not
        # crash, regardless of ordering or how many times we dump the AST
        # in between.
        self.expect_expr("hA.id", result_type="int", result_value="1")
        self.expect_expr("hB.tag", result_type="long", result_value="2")

        # Deliberately reinterpret the smaller 'v1' layout as the larger
        # 'v2' layout (and vice versa). This should never crash LLDB (it
        # is well-defined-enough from LLDB's perspective: a plain memory
        # read at a computed offset), even though the result is
        # meaningless/wrong from the target program's point of view.
        self.expect_expr("((__typeof__(hB) *)&hA)->tag", result_type="long")
        self.expect_expr("((__typeof__(hA) *)&hB)->id", result_type="int")
