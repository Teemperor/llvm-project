import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstNestedUsingShadowRecursiveOdrTestCase(TestBase):
    def test_combined_member_access(self):
        """
        Puts LLDB's ASTImporter/DWARFASTParserClang machinery into a weird
        state via a *double-chained* using-declaration ODR conflict:

          main.cpp:
            namespace outer { namespace inner { struct Data { int v; }; }
                               using inner::Data; }
            using outer::Data;
            Data da{1};

          plugin.cpp (dylib):
            namespace outer { namespace inner {
                struct Data { int v; int w; long x; }; }
                using inner::Data; }
            using outer::Data;
            Data db{1, 2, 3};

        Both sides build the exact same *shape* of nested using-declaration
        chain (a using-decl inside namespace outer re-exporting inner::Data,
        and a global using-decl re-exporting outer::Data), but the
        underlying struct that chain ultimately refers to has a different
        layout on each side. When LLDB has to materialize one of these
        using-declarations as a real Decl (see
        DWARFASTParserClang::GetClangDeclForDIE's DW_TAG_imported_declaration
        handling), it builds a clang::UsingDecl/UsingShadowDecl whose target
        can itself be another UsingShadowDecl - i.e. a chain of shadow decls
        of depth two before bottoming out at the actual (ODR-conflicting)
        CXXRecordDecl.

        This first checks that dumping each module's independent AST (which
        may or may not have materialized any of that chain yet, depending
        on what has already been looked up) doesn't crash, and that
        combining both conflicting 'Data' globals from the two modules in a
        single expression evaluates to the right value instead of crashing
        LLDB.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "main", lldb.SBFileSpec("main.cpp"))

        # Force each module's own (independent) AST to get a chance to
        # materialize whatever it already knows about 'Data' - this should
        # never crash, regardless of whether the using-declaration chain
        # has been touched yet.
        self.expect("target modules dump ast --filter Data -- a.out")
        self.expect("target modules dump ast --filter Data -- libplugin.dylib")

        # Now combine both conflicting definitions of 'Data' (1 field vs. 3
        # fields) in a single expression. This forces LLDB to resolve
        # da/db against their respective (ODR-conflicting) per-module
        # types and should produce the right arithmetic result rather than
        # crashing or reading uninitialized memory.
        self.expect_expr("da.v + db.w + db.x", result_type="long", result_value="6")

        # Dump again after the combined expression, in case evaluating it
        # changed what either module's independent AST or the shared
        # scratch AST now contains.
        self.expect("target modules dump ast --filter Data -- a.out")
        self.expect("target modules dump ast --filter Data -- libplugin.dylib")
        self.expect("target dump typesystem")

    @expectedFailureAll(
        bugnumber="LLDB's DWARF-based name lookup does not resolve a type "
        "name through a chain of using-declarations (DW_TAG_imported_"
        "declaration): looking up the re-exported name 'outer::Data' (let "
        "alone the doubly re-exported global './Data') fails with a "
        "spurious 'no member named Data in namespace outer' / 'use of "
        "undeclared identifier Data' instead of finding the underlying "
        "struct, even though the DWARF debug info for the using-declaration "
        "is present. Additionally, once one module's 'outer::inner::Data' "
        "has been imported into the shared scratch AST context, declaring "
        "a local variable of the (spelled identically, but ODR-conflicting) "
        "'outer::inner::Data' from the *other* module produces a confusing "
        "'no viable conversion from outer::inner::Data to outer::inner::"
        "Data' diagnostic instead of either succeeding or reporting the "
        "real ODR conflict clearly"
    )
    def test_using_chain_and_scratch_ast_conflict(self):
        """
        Documents two related, real limitations uncovered while exercising
        the nested using-declaration chain from test_combined_member_access:

        1. The re-exported names 'outer::Data' and the global 'Data'
           (introduced by the using-declarations) cannot actually be looked
           up through LLDB's expression evaluator at all, even though the
           underlying 'outer::inner::Data' can.

        2. Once the main executable's 'outer::inner::Data' has been pulled
           into the shared per-target scratch AST context (by declaring a
           local variable of that type initialized from 'da'), trying to do
           the same thing with the dylib's ODR-conflicting
           'outer::inner::Data' (initialized from 'db') fails with a
           confusing same-looking-name conversion error, because the
           scratch AST now holds only the first module's version under that
           qualified name.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "main", lldb.SBFileSpec("main.cpp"))

        # The two levels of using-declarations should make 'Data' visible
        # as 'outer::Data' (and, once more, as the unqualified global
        # 'Data'), re-exporting inner::Data. Today this lookup fails.
        self.expect_expr("outer::Data{}.v", result_value="0")

        # Force the main executable's 'outer::inner::Data' into the shared
        # scratch AST context.
        self.expect_expr("outer::inner::Data ddd = da; ddd.v", result_value="1")

        # Now try to do the same with the dylib's ODR-conflicting
        # 'outer::inner::Data'. Today this fails with a spurious
        # "no viable conversion" error because the scratch AST already has
        # a same-named-but-different 'outer::inner::Data' cached from the
        # main executable.
        self.expect_expr("outer::inner::Data ddd2 = db; ddd2.w", result_value="2")
