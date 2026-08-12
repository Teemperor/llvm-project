"""
Test LLDB's handling of an ODR violation where the *only* difference
between two conflicting definitions of the same class is an
AccessSpecifier (public vs. private), both on a data member and on a
base class.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstPrivateVsPublicMemberOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's expression evaluator against two ODR-violating
        definitions of 'Secret' that differ *only* in the AccessSpecifier
        of their 'value' member:
          - main.cpp:   class Secret { public: int value; ...  };
          - plugin.cpp: class Secret { private: int value; ... };

        Both have the same field name, type, and offset, so this
        conflict is invisible to anything that only looks at
        size/layout. The idea was that ASTImporter merging two
        same-named FieldDecls that only differ in getAccess() might
        desync Sema's access-control bookkeeping (Sema::CheckMemberAccess)
        or DeclContext's decl-chain iteration and crash.

        In practice this can't happen for *data members*: LLDB's DWARF
        parser (DWARFASTParserClang::ParseChildMembers /
        TypeSystemClang::AddFieldToRecordType) never reads
        DW_AT_accessibility for a DW_TAG_member at all and unconditionally
        marks every imported FieldDecl 'public' (AS_public), regardless
        of what AccessSpecifier the field actually had in the source
        that produced the debug info. So LLDB's Clang AST never actually
        observes this particular ODR conflict: both 'Secret::value'
        FieldDecls end up 'public' in LLDB's AST, and reading the
        "private" field from outside the class trivially succeeds. On
        top of that, LLDB also disables Clang's access-control checking
        entirely for expressions (ClangExpressionParser sets
        'LangOpts.AccessControl = false', "Debuggers get universal
        access"), so even if the AccessSpecifier were preserved, nothing
        would ever enforce it.

        This test documents that (unsurprising, non-crashing) behavior:
        reading the "private" field from the dylib's globals, or from the
        main executable's globals, from an expression evaluated while
        stopped in the dylib, works and returns the right value either
        way.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Read the "private" dylib field directly. LLDB's access-control
        # checking is disabled for expressions, and (independently of
        # that) LLDB's DWARF parser never even preserves per-field
        # AccessSpecifiers, so this succeeds instead of erroring out with
        # a "private member" diagnostic.
        self.expect_expr("secretFromPlugin.value", result_value="99")

        # Now, from the same frame, also read the main executable's
        # (public) 'Secret::value' through a cast of the raw pointer that
        # was passed into plugin_entry. This forces LLDB to import/merge
        # both modules' conflicting 'Secret' definitions -- one with a
        # public 'value', one with a private 'value' -- into the shared
        # per-target scratch AST context in the same expression.
        self.expect_expr(
            "((Secret*)secretFromMainExePtr)->value", result_value="42"
        )

        # Do it again, the other way around, and combine both in a single
        # expression, to exercise the merged/reconciled scratch AST
        # RecordDecl for 'Secret' further.
        self.expect_expr(
            "((Secret*)secretFromMainExePtr)->value + secretFromPlugin.value",
            result_value="141",
        )

        # Print each module's (conflicting) 'Secret' individually and
        # together; none of this should crash while materializing the
        # (possibly Frankenstein-merged) type.
        self.expect_expr("secretFromPlugin")
        self.expect_expr("*(Secret*)secretFromMainExePtr")

        # sizeof() should agree for both (identical layout) and shouldn't
        # crash while laying out the merged RecordDecl.
        self.frame().EvaluateExpression("(int)sizeof(Secret)")
        self.frame().EvaluateExpression("(int)sizeof(secretFromPlugin)")

        # 'type lookup' walks/prints the *unmerged*, per-module
        # definitions from each module's own DWARF-derived AST (as
        # opposed to the scratch AST importer's merged result above).
        # Both should print 'value' the same way (LLDB doesn't surface
        # field access specifiers at all), and this shouldn't crash.
        self.expect("type lookup Secret", substrs=["class Secret"])

        # Second, related ODR conflict: a *base class*'s accessibility
        # (rather than a data member's) differs between the two modules --
        # main.cpp's 'Derived' inherits from 'Base' publicly, the dylib's
        # same-named 'Derived' inherits privately. Unlike a data member's
        # AccessSpecifier, DWARFASTParserClang::ParseInheritance() *does*
        # read DW_AT_accessibility for DW_TAG_inheritance and preserves it
        # on the imported CXXBaseSpecifier, so this is a real, observable
        # AccessSpecifier ODR conflict in LLDB's Clang AST (visible via
        # 'type lookup', below) -- though access-control enforcement is
        # still globally disabled for expressions, so it doesn't affect
        # expression evaluation either.
        self.expect_expr("derivedFromPlugin.getValue()", result_value="199")
        self.expect_expr("derivedFromPlugin.value", result_value="199")

        # Upcast through the privately-inherited 'Base' from an
        # expression; this only compiles at all because access-control
        # checking is disabled for LLDB expressions.
        self.expect_expr(
            "((Base*)&derivedFromPlugin)->value", result_value="199"
        )

        # 'type lookup Derived' should show both modules' conflicting
        # 'Derived' definitions (one 'public Base', one 'private Base')
        # without crashing while walking/printing the merged or
        # per-module CXXBaseSpecifier list.
        self.expect("type lookup Derived", substrs=["class Derived"])
