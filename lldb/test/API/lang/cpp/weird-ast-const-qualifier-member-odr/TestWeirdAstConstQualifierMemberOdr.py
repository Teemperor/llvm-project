import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstConstQualifierMemberOdrTestCase(TestBase):
    def test(self):
        """
        Tests LLDB's behaviour when the same struct names ('Reading' and
        'Sensor') are defined with a const-qualifier ODR violation between
        the main executable and a dylib. main.cpp's 'Reading::value' is
        'const int', while the dylib's 'Reading::value' is a plain 'int'.
        Both definitions of 'Reading' (and of the enclosing 'Sensor',
        which embeds a 'Reading' member) have identical size/byte layout,
        so the conflict is invisible to anything that only compares
        offsets/sizes.

        However, the const-qualified member means main.cpp's 'Reading'
        (and 'Sensor') implicitly lose their copy-assignment operator and
        default constructor, while the dylib's identically-laid-out
        'Reading'/'Sensor' keep theirs. When LLDB's ASTImporter merges the
        two conflicting definitions into the shared scratch AST context,
        it may end up with a Frankenstein RecordDecl whose special member
        functions don't match either original definition, which is a
        plausible way to confuse the expression JIT's handling of
        construction/copy-assignment for the merged type (rather than
        just producing a wrong answer).
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Evaluate both globals individually first.
        self.expect_expr("gMainSensor.reading.value", result_value="111")
        self.expect_expr("gMainSensor.id", result_value="1")
        self.expect_expr("gPluginSensor.reading.value", result_value="222")
        self.expect_expr("gPluginSensor.id", result_value="2")

        # Print/materialize both modules' (conflicting) 'Sensor'
        # definitions individually.
        self.expect_expr("gMainSensor")
        self.expect_expr("gPluginSensor")

        # Now evaluate an expression that references both modules'
        # conflicting 'Sensor'/'Reading' definitions together, forcing
        # the ASTImporter to import/complete both into the shared scratch
        # AST context at once.
        self.expect_expr(
            "gMainSensor.reading.value + gPluginSensor.reading.value",
            result_value="333",
        )

        # Construct a fresh 'Sensor' value in the expression, which
        # exercises whichever notion of the (possibly Frankenstein-merged)
        # type's default constructor / initializer handling the JIT picks.
        self.frame().EvaluateExpression("Sensor{{333}, 3}")

        # Copy-assign between the two modules' same-named, same-layout,
        # but ODR-conflicting 'Sensor' objects. main.cpp's 'Sensor' should
        # have an implicitly-deleted copy-assignment operator (because of
        # the const member), while the dylib's should not; whichever
        # definition the merged AST node ends up exposing, this should not
        # crash LLDB while JITting/executing the assignment.
        self.frame().EvaluateExpression("gPluginSensor = gPluginSensor")

        # sizeof() on each module's notion of 'Sensor' should agree (both
        # are identically laid out), and should not crash while computing
        # the record layout for the conflicting merged decl.
        self.frame().EvaluateExpression("(int)sizeof(gMainSensor)")
        self.frame().EvaluateExpression("(int)sizeof(gPluginSensor)")
