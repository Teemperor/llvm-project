"""
Test basic string functionality.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestSharedPtr(TestBase):

    mydir = TestBase.compute_mydir(__file__)


    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
                                          "// Set break point at this line.",
                                          lldb.SBFileSpec("main.cpp"))

        self.runCmd("settings set target.import-std-module true")

        string_type = "std::basic_string<char, std::char_traits<char>, std::allocator<char> >"
        size_type = string_type + "::size_type"
        value_type = string_type + "::value_type"
        iterator_type = string_type + "::iterator"
        riterator_type = string_type + "::reverse_iterator"

        self.expect_expr("s",
                         result_type="std::string",
                         result_summary='"str"')
        self.expect_expr("s.size()", result_type=size_type, result_value="3")
        self.expect_expr("s.length()", result_type=size_type, result_value="3")
        self.expect_expr("s.empty()", result_type="bool", result_value="false")
        self.expect_expr("s.front()", result_type=value_type, result_value="'s'")
        self.expect_expr("s.back()", result_type=value_type, result_value="'r'")

        self.expect_expr("s.begin()", result_type=iterator_type, result_children=[
            ValueCheck(name="item", value="'s'")
        ])
        self.expect_expr("s.rbegin()", result_type=riterator_type, result_children=[
            ValueCheck(name="__t"),
            ValueCheck(name="current")
        ])
        self.expect_expr("s.substr(0, 1)",
                         result_type=string_type,
                         result_summary='"s"')
        self.expect_expr("s.find(\"other\") == std::string::npos",
                         result_type="bool",
                         result_value="true")

        # Append a 'f' to the string.
        self.expect("expr s += \"f\"")
        self.expect_expr("s.back()", result_type=value_type, result_value="'f'")
        # Drop the 'f' again.
        self.expect("expr s.resize(3)")
        self.expect_expr("s.back()", result_type=value_type, result_value="'r'")

        # Test std::basic_string<char> (std::string without the typedef).
        self.expect_expr("cs",
                         result_type=string_type,
                         result_summary='"str"')
        self.expect_expr("cs.size()", result_type=size_type, result_value="3")
        self.expect_expr("cs.length()", result_type=size_type, result_value="3")
        self.expect_expr("cs.empty()", result_type="bool", result_value="false")
        self.expect_expr("cs.front()", result_type=value_type, result_value="'s'")
        self.expect_expr("cs.back()", result_type=value_type, result_value="'r'")


        # std::wstring
        string_type = "std::basic_string<wchar_t, std::char_traits<wchar_t>, std::allocator<wchar_t> >"
        size_type = string_type + "::size_type"
        value_type = string_type + "::value_type"

        self.expect_expr("ws",
                         result_type="std::wstring",
                         result_summary='L"str"')
        self.expect_expr("ws.size()", result_type=size_type, result_value="3")
        # FIXME: The string printer provides an incorrect summary.
        self.expect_expr("ws.front()", result_type=value_type, result_value="s\\0\\0\\0")

        # std::u16string
        string_type = "std::basic_string<char16_t, std::char_traits<char16_t>, std::allocator<char16_t> >"
        size_type = string_type + "::size_type"
        value_type = string_type + "::value_type"

        self.expect_expr("s16",
                         result_type="std::u16string",
                         result_summary='u"str"')
        self.expect_expr("s16.size()", result_type=size_type, result_value="3")
        # FIXME: The string printer provides an incorrect summary.
        self.expect_expr("s16.front()", result_type=value_type, result_value="U+0073")

        # std::u32string
        string_type = "std::basic_string<char32_t, std::char_traits<char32_t>, std::allocator<char32_t> >"
        size_type = string_type + "::size_type"
        value_type = string_type + "::value_type"

        self.expect_expr("s32",
                         result_type="std::u32string",
                         result_summary='U"str"')
        self.expect_expr("s32.size()", result_type=size_type, result_value="3")
        # FIXME: The string printer provides an incorrect summary.
        self.expect_expr("s32.front()", result_type=value_type, result_value="U+0x00000073")
