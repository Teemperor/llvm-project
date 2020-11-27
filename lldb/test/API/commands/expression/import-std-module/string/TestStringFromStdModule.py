"""
Test basic std::string functionality.
"""

from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestBasicVector(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def basic_string_check(self, var, string_type):
        size_type = "std::basic_string<char>::size_type"
        iterator_type = "std::basic_string<char>::iterator"
        self.expect_expr(var, result_type=string_type,
                         result_summary='"abc"')
        self.expect_expr(var + ".size()", result_type=size_type, result_value="3")
        self.expect_expr(var + "[0]", result_value="'a'")
        self.expect_expr(var + ".front()", result_value="'a'")
        self.expect("expr " + var + ".push_back('d')")
        self.expect_expr(var + ".back()", result_value="'d'")
        self.expect("expr " + var + ".pop_back()")
        self.expect_expr(var + ".data()", result_summary='"abc"')
        self.expect_expr(var + ".c_str()", result_summary='"abc"')
        self.expect_expr(var + ".begin()", result_type=iterator_type)
        self.expect_expr("*" + var + ".begin()", result_value="'a'")
        self.expect_expr(var + ".end()", result_type=iterator_type)
        self.expect("expr " + var + ".clear()")
        self.expect_expr(var + ".empty()", result_value="true")
        self.expect("expr " + var + ".append(\"1\")")
        self.expect("expr " + var + " += \"2\"")
        self.expect_expr(var + ".compare(\"12\")", result_value="0")
        self.expect_expr(var + ".find('2')", result_value="1")
        self.expect_expr(var + ".find(' ') == std::string::npos", result_value="true")


    @add_test_categories(["libc++"])
    @skipIf(compiler=no_match("clang"))
    def test(self):
        self.build()

        lldbutil.run_to_source_breakpoint(self,
                                          "// Set break point at this line.",
                                          lldb.SBFileSpec("main.cpp"))

        self.runCmd("settings set target.import-std-module true")

        # std::string
        self.basic_string_check("s", "std::string")
        self.basic_string_check("bs", "std::basic_string<char>")

        # std::wstring
        string_type = "std::wstring"
        size_type = "std::basic_string<wchar_t>::size_type"
        iterator_type = "std::basic_string<wchar_t>::iterator"
        self.expect_expr("ws", result_type=string_type,
                         result_summary='L"abc"')
        self.expect_expr("ws.size()", result_type=size_type, result_value="3")
        self.expect_expr("ws[0]", result_summary="L'a'")
        self.expect_expr("ws.front()", result_summary="L'a'")
        self.expect("expr ws.push_back(L'd')")
        self.expect_expr("ws.back()", result_summary="L'd'")
        self.expect("expr ws.pop_back()")
        self.expect_expr("ws.data()", result_summary='L"abc"')
        self.expect_expr("ws.c_str()", result_summary='L"abc"')
        self.expect_expr("ws.begin()", result_type=iterator_type)
        self.expect_expr("*ws.begin()", result_summary="L'a'")
        self.expect_expr("ws.end()", result_type=iterator_type)
        self.expect("expr ws.clear()")
        self.expect_expr("ws.empty()", result_value="true")
        self.expect("expr ws.append(L\"1\")")
        self.expect("expr ws += L\"2\"")
        self.expect_expr("ws.compare(L\"12\")", result_value="0")
        self.expect_expr("ws.find(L'2')", result_value="1")
        self.expect_expr("ws.find(L' ') == std::wstring::npos", result_value="true")

        # std::u16string
        string_type = "std::u16string"
        size_type = "std::basic_string<char16_t>::size_type"
        iterator_type = "std::basic_string<char16_t>::iterator"
        self.expect_expr("s16.size()", result_type=size_type, result_value="3")
        self.expect_expr("s16[0]", result_summary="U+0061 u'a'")
        self.expect_expr("s16.front()", result_summary="U+0061 u'a'")
        self.expect("expr s16.push_back(u'd')")
        self.expect_expr("s16.back()", result_summary="U+0064 u'd'")
        self.expect("expr s16.pop_back()")
        self.expect_expr("s16.data()", result_summary='u"abc"')
        self.expect_expr("s16.c_str()", result_summary='u"abc"')
        self.expect_expr("s16.begin()", result_type=iterator_type)
        self.expect_expr("*s16.begin()", result_summary="U+0061 u'a'")
        self.expect_expr("s16.end()", result_type=iterator_type)
        self.expect("expr s16.clear()")
        self.expect_expr("s16.empty()", result_value="true")
        self.expect("expr s16.append(u\"1\")")
        self.expect("expr s16 += u\"2\"")
        self.expect_expr("s16.compare(u\"12\")", result_value="0")
        self.expect_expr("s16.find(u'2')", result_value="1")
        self.expect_expr("s16.find(u' ') == std::u16string::npos", result_value="true")

        # std::u32string
        string_type = "std::u32string"
        size_type = "std::basic_string<char32_t>::size_type"
        iterator_type = "std::basic_string<char32_t>::iterator"
        self.expect_expr("s32", result_type=string_type,
                         result_summary='U"abc"')
        self.expect_expr("s32.size()", result_type=size_type, result_value="3")
        self.expect_expr("s32[0]", result_summary="U+0x00000061 U'a'")
        self.expect_expr("s32.front()", result_summary="U+0x00000061 U'a'")
        self.expect("expr s32.push_back(U'd')")
        self.expect_expr("s32.back()", result_summary="U+0x00000064 U'd'")
        self.expect("expr s32.pop_back()")
        self.expect_expr("s32.data()", result_summary='U"abc"')
        self.expect_expr("s32.c_str()", result_summary='U"abc"')
        self.expect_expr("s32.begin()", result_type=iterator_type)
        self.expect_expr("*s32.begin()", result_summary="U+0x00000061 U'a'")
        self.expect_expr("s32.end()", result_type=iterator_type)
        self.expect("expr s32.clear()")
        self.expect_expr("s32.empty()", result_value="true")
        self.expect("expr s32.append(U\"1\")")
        self.expect("expr s32 += U\"2\"")
        self.expect_expr("s32.compare(U\"12\")", result_value="0")
        self.expect_expr("s32.find(U'2')", result_value="1")
        self.expect_expr("s32.find(U' ') == std::u16string::npos", result_value="true")
