"""
Test std::forward_list functionality with a decl from debug info as content.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestDbgInfoContentForwardList(TestBase):
    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    @skipIf(macos_version=["<", "15.0"])
    @skipIf(macos_sdk_version=["<", "16.0"])
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "// Set break point at this line.", lldb.SBFileSpec("main.cpp")
        )

        self.runCmd("settings set target.import-std-module true")

        if self.expectedCompiler(["clang"]) and self.expectedCompilerVersion(
            [">", "16.0"]
        ):
            list_type = "std::forward_list<Foo>"
        else:
            list_type = "std::forward_list<Foo, std::allocator<Foo> >"

        value_type = "value_type"

        if self.dbg.GetSetting("symbols.enable-typesystem-cpp").GetBooleanValue():
            # TypeSystemCpp computes the correct size and contents here (the
            # element type reconstructed from debug info lays out identically to
            # the std-module type), so the FIXME below no longer applies.
            self.expect_expr(
                "a",
                result_type=list_type,
                result_summary="size=3",
                result_children=[
                    ValueCheck(children=[ValueCheck(name="a", value="3")]),
                    ValueCheck(children=[ValueCheck(name="a", value="1")]),
                    ValueCheck(children=[ValueCheck(name="a", value="2")]),
                ],
            )
        else:
            # FIXME: This has three elements in it but the formatter seems to
            # calculate the wrong size and contents.
            self.expect_expr("a", result_type=list_type, result_summary="size=1")
        self.expect_expr("std::distance(a.begin(), a.end())", result_value="3")
        self.expect_expr("a.front().a", result_type="int", result_value="3")
        self.expect_expr("a.begin()->a", result_type="int", result_value="3")
