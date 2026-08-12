"""
Test LLDB's handling of an ODR violation where the same-named 'Meters'
class has both a user-defined conversion operator to 'double' AND a
befriended free 'operator+(Meters, Meters)', but with completely
different member layouts and operator bodies in the main executable
versus a dylib.

This tries to put LLDB's internal Clang AST (ASTImporter/TypeSystemClang/
DWARFASTParserClang machinery, and the per-target shared scratch
ASTContext) into a state where overload resolution for a mixed-type
'ma + mb' expression must rank conversion sequences through two distinct
'Meters::operator double() const' CXXConversionDecls (attached to
differently-laid-out 'Meters' RecordDecls sharing a name), while also
juggling ambiguity between two structurally-conflicting friend
'operator+(Meters, Meters)' candidates -- in the hope that this crashes
LLDB outright rather than merely producing a wrong-but-well-formed
value.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstOperatorConversionFriendOdrTestCase(TestBase):
    def test_ma_alone(self):
        """
        The main executable's 'Meters' (single 'int mm' member, operator
        double() divides by 1000) behaves correctly on its own, before
        the dylib's conflicting 'Meters' has been imported into the
        shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("(double)ma", result_value="1")
        self.expect_expr("ma + ma", result_type="Meters")
        self.expect_expr("(ma + ma).mm", result_value="2000")

    def test_mb_alone(self):
        """
        The dylib's 'Meters' (a 'double mm' plus an extra 'float
        precision' member, operator double() returns 'mm' directly)
        behaves correctly on its own, before the main executable's
        conflicting 'Meters' has been imported into the shared
        per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("(double)mb", result_value="1.5")
        self.expect_expr("mb + mb", result_type="Meters")
        self.expect_expr("(mb + mb).mm", result_value="3")

    def test_mixed_conversion_addition_does_not_crash(self):
        """
        Evaluates '(double)ma + (double)mb' and 'ma + mb' -- both of
        which pull the main executable's and the dylib's conflicting
        'Meters' definitions into the shared scratch AST context in the
        same expression. This forces Clang's overload resolution to
        consider two distinct 'Meters::operator double() const'
        CXXConversionDecls (whose 'Meters' RecordDecls have completely
        different member layouts despite sharing the tag name 'Meters'),
        as well as two structurally-conflicting friend
        'operator+(Meters, Meters)' candidates from each module.

        Since 'ma' and 'mb' are genuinely different C++ types from the
        parser's point of view (nothing here forces them into a single
        merged/canonical RecordDecl), the only common ground for '+' is
        each side's own (different) 'operator double()' followed by the
        built-in 'double + double': this should never crash LLDB, no
        matter how differently laid out or behaved the two 'Meters'
        really are.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("(double)ma + (double)mb", result_value="2.5")

        # 'ma + mb' can only be well-formed via each side's conversion
        # operator to 'double' followed by the builtin 'double + double'
        # (the friend 'operator+(Meters, Meters)' overloads don't apply
        # across two structurally different 'Meters' types), so this
        # should produce the same result as the explicit cast above.
        self.expect_expr("ma + mb", result_value="2.5")

        # Dumping the shared scratch typesystem and each module's AST
        # (filtered to 'Meters') after evaluating the mixed expression
        # above forces LLDB's ASTImporter/DWARFASTParserClang machinery
        # to traverse whatever state the two conflicting 'Meters'
        # CXXRecordDecls (and their CXXConversionDecls/friend
        # operator+ FunctionDecls) ended up in. This should never
        # crash LLDB.
        self.expect("target dump typesystem")
        self.expect("target modules dump ast --filter Meters")

    def test_repeated_mixed_expressions_and_dumps_do_not_crash(self):
        """
        Repeatedly evaluates the mixed-type conversion/addition
        expressions above, interleaved with AST/typesystem dumps, to
        stress-test the ASTImporter/DWARFASTParserClang machinery for a
        crash under repeated re-entry (e.g. repeatedly re-resolving
        overload candidates for the same ambiguous-looking friend
        operator+ signatures) rather than a single one-shot evaluation.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        for _ in range(5):
            self.expect_expr("ma + mb", result_value="2.5")
            self.expect_expr("mb + ma", result_value="2.5")
            self.expect("target modules dump ast --filter Meters")
            self.expect_expr("ma + ma", result_type="Meters")
            self.expect_expr("mb + mb", result_type="Meters")
            self.expect("target dump typesystem")

    @expectedFailureAll(
        bugnumber="once the scratch AST context's canonical 'Meters' has "
        "been fixed to one module's definition by persisting a value of "
        "that module's 'Meters' into a $-prefixed persistent expression "
        "variable, trying to persist the *other*, ODR-conflicting "
        "module's same-named 'Meters' global into a second persistent "
        "variable is rejected with a nonsensical 'no viable conversion "
        "from Meters to Meters' error, even though evaluating the same "
        "global on its own (without persisting it) still produces the "
        "correct value"
    )
    def test_persisting_both_conflicting_meters_is_rejected(self):
        """
        Documents a real limitation: once one module's 'Meters' has been
        pulled into the shared scratch AST context via a persistent
        expression result variable, persisting the *other* conflicting
        module's 'Meters' into a second persistent variable fails
        outright, because the bare type name 'Meters' in the second
        expression resolves against the scratch context's now-fixed
        (first module's) 'Meters' RecordDecl instead of the second
        module's.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Persist the main executable's 'Meters' first.
        self.expect_expr("Meters $p1 = ma")

        # Persisting the dylib's conflicting 'Meters' second should also
        # succeed (this is exactly analogous to 'Meters $p1 = ma' above,
        # just for the other module's definition), but currently fails
        # because the scratch context's 'Meters' has already been fixed
        # to the main executable's version.
        self.expect_expr("Meters $p2 = mb")
