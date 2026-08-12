import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstDefaultedEqOperatorDifferentMembersOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each definition of 'Point3' -- and its compiler-synthesized
        defaulted 'operator==' -- can be read/printed/compared fine on its
        own, as long as the *other* conflicting definition hasn't already
        been imported into the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "point1 == point1", result_type="bool", result_value="true"
        )
        self.expect_expr(
            "point2 == point2", result_type="bool", result_value="true"
        )

        # Dump both modules' Clang ASTs for 'Point3', to document the
        # layout/member-type mismatch (the exe's 'z' is 'int', the
        # dylib's 'z' is 'long') that the ASTImporter has to deal with
        # once both are referenced together (see test_both_together).
        self.expect(
            "target modules dump ast --filter Point3",
            substrs=[
                "struct Point3",
                "x 'int'",
                "y 'int'",
                "z 'int'",
                "z 'long'",
            ],
        )

    @expectedFailureAll(
        bugnumber="LLDB's ASTImporter never merges the fields of two "
        "genuinely ODR-violating CXXRecordDecls named 'Point3' (the "
        "exe's has an 'int z', the dylib's has a 'long z'): whichever "
        "one is imported into the scratch AST context first stays "
        "there unmodified, and the second one is reconstructed as a "
        "second, physically distinct CXXRecordDecl with the same name. "
        "Comparing one against the other with the compiler-synthesized "
        "defaulted 'operator==' therefore fails with a confusing "
        "'invalid operands to binary expression' diagnostic (as if "
        "'Point3' and 'Point3' were unrelated, incomparable types) "
        "instead of either evaluating correctly or reporting the real "
        "ODR conflict."
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when the exact same class ('Point3') has
        two conflicting C++20 definitions -- one in the main executable,
        one in a dylib -- that agree on the *number* of data members but
        disagree on the type (and therefore size/layout) of the last one:
        'int z' in the exe vs. 'long z' in the dylib. Both sides declare a
        C++20 defaulted comparison operator
        ('friend bool operator==(const Point3&, const Point3&) = default')
        that Clang synthesizes lazily, by walking the field list of
        whichever 'Point3' definition is in scope at the point of first
        use.

        Using both conflicting definitions of 'Point3' in the same
        expression forces LLDB's ASTImporter to reconcile two
        CXXRecordDecls for 'Point3' with different field lists (and
        different synthesized 'operator==' bodies as a result). This
        should ideally either evaluate sensibly (treating the two as
        distinct, unrelated types that simply can't be compared, with a
        clear diagnostic) or otherwise fail gracefully, but not crash.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Reference both conflicting definitions of 'Point3' in the same
        # debug session first, forcing the ASTImporter to reconcile the
        # two conflicting CXXRecordDecls.
        self.expect_expr(
            "point1 == point1", result_type="bool", result_value="true"
        )
        self.expect_expr(
            "point2 == point2", result_type="bool", result_value="true"
        )

        # This combines both conflicting definitions of 'Point3' in a
        # single expression, forcing Clang's Sema to type-check a binary
        # '==' between two operands that are each named 'Point3' but come
        # from different, ODR-violating CXXRecordDecls. A real C++
        # program could never observe this (it would just be a plain ODR
        # violation, likely "resolved" by the linker picking one
        # definition), so there's no single "correct" answer here -- but
        # this must not crash LLDB. Ideally this would evaluate to 'true'
        # (all three members compare equal, 1/2/3 on both sides), but
        # LLDB currently rejects it outright.
        self.expect_expr(
            "point1 == point2", result_type="bool", result_value="true"
        )
