"""
Test importing declarations from modules via a series of intermediate modules.
"""

import lldb
import os
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestWithGmodulesDebugInfo(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    @add_test_categories(["gmodules"])
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, "// break here", lldb.SBFileSpec("main.cpp"))

        # Accessing structs.
        self.expect_expr("s", result_type="GlobalStruct", result_children=[
          ValueCheck(name="i", type="int", value="1")
        ])
        self.expect_expr("ns", result_type="NS::NSStruct", result_children=[
          ValueCheck(name="i", type="int", value="2")
        ])
        self.expect_expr("ins", result_type="NS::InlineNSStruct", result_children=[
          ValueCheck(name="i", type="int", value="3")
        ])


        # Templates in the namespace
        self.expect_expr("template_s", result_type="NS::NSTemplateStruct<int>", result_children=[
          ValueCheck(name="ns", type="NS::NSStruct"),
          ValueCheck(name="field", type="int", value="7"),
          ValueCheck(name="inline_ns", type="NS::InlineNSStruct"),
          # FIXME: This is missing namespace/class scopes before 'Type'
          ValueCheck(name="typedefd", type="Type"),
        ])

        self.expect_expr("spez", result_type="NS::NSTemplateStructWithSpez<GlobalStruct>", result_children=[
          ValueCheck(name="i", type="long", value="5"),
          ValueCheck(name="typedefd", type="NS::NSTemplateStructWithSpez<GlobalStruct>::Type"),
        ])

        self.expect_expr("spez_int", result_type="NS::NSTemplateStructWithSpez<int>", result_children=[
          ValueCheck(name="t", type="int", value="0"),
          # FIXME: This is missing namespace/class scopes before 'Type'
          ValueCheck(name="typedefd", type="Type"),
        ])


        # Templates in the inline namespace
        self.expect_expr("inline_template", result_type="NS::InlineNSTemplateStruct<int>", result_children=[
          ValueCheck(name="ns", type="NS::NSStruct"),
          ValueCheck(name="field", type="int", value="6"),
          ValueCheck(name="inline_ns", type="NS::InlineNSStruct"),
          # FIXME: This is missing namespace/class scopes before 'Type'
          ValueCheck(name="typedefd", type="Type"),
        ])

        self.expect_expr("inline_spez", result_type="NS::InlineNSTemplateStructWithSpez<GlobalStruct>", result_children=[
          ValueCheck(name="i", type="long", value="4"),
          ValueCheck(name="typedefd", type="NS::InlineNSTemplateStructWithSpez<GlobalStruct>::Type"),
        ])

        self.expect_expr("inline_spez_int", result_type="NS::InlineNSTemplateStructWithSpez<int>", result_children=[
          ValueCheck(name="i", type="int", value="0"),
          # FIXME: This is missing namespace/class scopes before 'Type'
          ValueCheck(name="typedefd", type="Type"),
        ])
