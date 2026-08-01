import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftVariadicGenericsNonArgumentPacks(TestBase):
    """Test expression evaluation in variadic generic functions that contain
    pack expansions which are not value pack parameters."""

    NO_DEBUG_INFO_TESTCASE = True

    @skipEmbeddedSwift
    @swiftTest
    # rdar://152465885 Address Sanitizer assert doing
    # `expr --bind-generic-types=false`.
    @skipIfAsan
    def test(self):
        self.build()

        target, process, _, _ = lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.swift"))

        # returnsPack(1, A(), B()): the pack expansion in the return type is
        # not passed in as a value pack. This function does have a value pack
        # parameter, so it can be evaluated without binding generic params.
        self.expect("frame variable",
                    substrs=["n = 1", "Pack{(a.A, a.B)}", "t",
                             "i = 23", "d = 2.71"])
        self.expect("expr -- n * 10", patterns=[r"\(Int\) \$R\d+ = 10"])
        self.expect("expr --bind-generic-types=false -- n * 10",
                    patterns=[r"\(Int\) \$R\d+ = 10"])

        # tupleParam(2, (A(), B())): the pack expansion is nested inside the
        # tuple parameter, so there is no value pack in the frame. The generic
        # expression evaluator doesn't support this, but the expression still
        # needs to produce the correct result by binding the generic params.
        process.Continue()
        self.expect("frame variable", substrs=["n = 2"])
        self.expect("expr -- n * 10", patterns=[r"\(Int\) \$R\d+ = 20"])
        self.expect("expr --bind-generic-types=false -- n * 10", error=True,
                    substrs=["Could not evaluate the expression without "
                             "binding generic types."])

        # functionParam(3) { ... }: the pack expansion is nested inside the
        # function type of the parameter.
        process.Continue()
        self.expect("frame variable", substrs=["n = 3"])
        self.expect("expr -- n * 10", patterns=[r"\(Int\) \$R\d+ = 30"])
        self.expect("expr --bind-generic-types=false -- n * 10", error=True,
                    substrs=["Could not evaluate the expression without "
                             "binding generic types."])

        # onlyInReturn(4): the only pack expansion is in the return type.
        process.Continue()
        self.expect("frame variable", substrs=["n = 4"])
        self.expect("expr -- n * 10", patterns=[r"\(Int\) \$R\d+ = 40"])
        self.expect("expr --bind-generic-types=false -- n * 10", error=True,
                    substrs=["Could not evaluate the expression without "
                             "binding generic types."])

        # mentionsPack(5, A(), B()): `w` isn't a value pack, even though its
        # type mentions a pack. Only `t` may be forwarded as a value pack.
        process.Continue()
        self.expect("frame variable",
                    substrs=["n = 5", "Pack{(a.A, a.B)}", "t",
                             "i = 23", "d = 2.71"])
        self.expect("expr -- n * 10", patterns=[r"\(Int\) \$R\d+ = 50"])
        self.expect("expr --bind-generic-types=false -- n * 10",
                    patterns=[r"\(Int\) \$R\d+ = 50"])
