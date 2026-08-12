import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstAnonNamespaceCrossTwoDylibsOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Tests LLDB's handling of two *separate* dylibs that are both loaded
        into the same process and that each independently define a struct
        named "Internal" inside their own unnamed (anonymous) namespace.

        Per the C++ standard, a name declared inside an unnamed namespace
        has internal linkage, so "Internal" in dylib_one.cpp and "Internal"
        in dylib_two.cpp are two completely unrelated types that merely
        happen to share a spelling - the equivalent of each dylib having
        used a random, private, TU-local name for its own bookkeeping
        struct. The two "Internal" structs also have different shapes
        (one int member vs. three int members), so they have different
        sizes.

        This is different from the existing odr-handling-with-dylib test,
        which has a single anonymous namespace whose name conflict shows up
        across a *single* binary + dylib pair sharing one translation unit's
        namespace instance. Here the anonymous namespaces are fully
        independent per-dylib, so a correct implementation must never try
        to unify these two types at all.

        Each dylib's "Internal" can be read fine on its own, as long as the
        *other* dylib's same-spelled "Internal" hasn't already been imported
        into the shared per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("g_one.a", result_value="111")
        self.expect("expr sizeof(g_one)", substrs=["4"])

    def test_each_alone_other_order(self):
        """
        Same as test_each_alone, but reads dylib_two's "Internal" on its own
        first (without dylib_one's same-spelled "Internal" already imported
        into the scratch AST context).
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("g_two.a", result_value="222")
        self.expect_expr("g_two.b", result_value="333")
        self.expect_expr("g_two.c", result_value="444")
        self.expect("expr sizeof(g_two)", substrs=["12"])

    @expectedFailureAll(
        bugnumber="LLDB's scratch AST context incorrectly conflates two "
        "same-spelled but otherwise unrelated 'Internal' types that each "
        "live inside their own dylib's unnamed (anonymous) namespace: such "
        "types have internal/TU-local linkage and no ODR relationship "
        "whatsoever across translation units (let alone across two "
        "distinct dylibs), yet LLDB's module-scoped type uniquing keys "
        "only on the qualified name '(anonymous namespace)::Internal' and "
        "ends up merging one 'Internal' using the other's layout, "
        "producing a silently wrong combined value instead of never "
        "unifying these two types at all"
    )
    def test_both_together(self):
        """
        Tests LLDB's behaviour when both differently-shaped, same-spelled
        anonymous-namespace "Internal" globals (one from each dylib) are
        referenced in a single expression, so that both get imported into
        the shared scratch AST context at the same time. Evaluating an
        expression that touches both globals at once exercises the (buggy)
        merge path and can produce a wrong value if the merged type ends up
        with a mismatched RecordLayout.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("g_one.a + g_two.a + g_two.b + g_two.c", result_value="666")
