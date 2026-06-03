"""
Test that lldb displays variables of all integer types correctly in C++.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        """Check the types and values of all integer-typed variables."""
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "break here", lldb.SBFileSpec("main.cpp")
        )

        # Check every scalar integer type both via 'frame variable' (var path)
        # and via the expression evaluator.
        self.expect_var_path("the_char", type="char", value="'a'")
        self.expect_expr("the_char", result_type="char", result_value="'a'")

        self.expect_var_path("the_signed_char", type="signed char", value="'B'")
        self.expect_expr("the_signed_char", result_type="signed char", result_value="'B'")

        self.expect_var_path("the_unsigned_char", type="unsigned char", value="'Z'")
        self.expect_expr(
            "the_unsigned_char", result_type="unsigned char", result_value="'Z'"
        )

        self.expect_var_path("the_short", type="short", value="-31987")
        self.expect_expr("the_short", result_type="short", result_value="-31987")

        self.expect_var_path("the_unsigned_short", type="unsigned short", value="65000")
        self.expect_expr(
            "the_unsigned_short", result_type="unsigned short", result_value="65000"
        )

        self.expect_var_path("the_int", type="int", value="-1100110")
        self.expect_expr("the_int", result_type="int", result_value="-1100110")

        self.expect_var_path("the_unsigned_int", type="unsigned int", value="4000000000")
        self.expect_expr(
            "the_unsigned_int", result_type="unsigned int", result_value="4000000000"
        )

        self.expect_var_path("the_long", type="long", value="-1100110100")
        self.expect_expr("the_long", result_type="long", result_value="-1100110100")

        self.expect_var_path("the_unsigned_long", type="unsigned long", value="1100110100")
        self.expect_expr(
            "the_unsigned_long", result_type="unsigned long", result_value="1100110100"
        )

        self.expect_var_path("the_long_long", type="long long", value="-110011001100")
        self.expect_expr(
            "the_long_long", result_type="long long", result_value="-110011001100"
        )

        self.expect_var_path(
            "the_unsigned_long_long",
            type="unsigned long long",
            value="110011001100",
        )
        self.expect_expr(
            "the_unsigned_long_long",
            result_type="unsigned long long",
            result_value="110011001100",
        )

        # Check the different "shapes" of the int type: array, pointer,
        # reference, struct, union and class.
        self.expect_var_path("the_int_array", type="int[3]")
        self.expect_var_path("the_int_array[0]", type="int", value="1")
        self.expect_var_path("the_int_array[1]", type="int", value="2")
        self.expect_var_path("the_int_array[2]", type="int", value="3")

        self.expect_var_path("*the_int_ptr", type="int", value="-1100110")
        self.expect_expr("*the_int_ptr", result_type="int", result_value="-1100110")

        self.expect_var_path("the_int_ref", type="int &")
        self.expect_expr("the_int_ref", result_type="int", result_value="-1100110")

        self.expect_var_path("the_int_struct.a", type="int", value="10")
        self.expect_var_path("the_int_struct.b", type="int", value="20")
        self.expect_expr("the_int_struct.a", result_type="int", result_value="10")

        self.expect_var_path("the_int_union.as_int", type="int", value="42")
        self.expect_expr("the_int_union.as_int", result_type="int", result_value="42")

        self.expect_var_path("the_int_class.m_a", type="int", value="50")
        self.expect_var_path("the_int_class.m_b", type="int", value="60")
        self.expect_expr("the_int_class.m_a", result_type="int", result_value="50")

        self.expect_var_path("the_int_class_ptr->m_a", type="int", value="50")
        self.expect_var_path("the_int_class_ref.m_b", type="int", value="60")

        # Check edge-case values: smallest, largest, zero and -1. The char-like
        # types are displayed as character literals.
        self.expect_var_path("char_zero", type="char", value="'\\0'")
        self.expect_var_path("schar_neg_one", type="signed char", value="'\\xff'")
        self.expect_var_path("schar_min", type="signed char", value="'\\x80'")
        self.expect_var_path("schar_max", type="signed char", value="'\\x7f'")
        self.expect_var_path("uchar_zero", type="unsigned char", value="'\\0'")
        self.expect_var_path("uchar_max", type="unsigned char", value="'\\xff'")

        self.expect_var_path("short_min", type="short", value="-32768")
        self.expect_var_path("short_max", type="short", value="32767")
        self.expect_var_path("short_zero", type="short", value="0")
        self.expect_var_path("short_neg_one", type="short", value="-1")
        self.expect_var_path("ushort_zero", type="unsigned short", value="0")
        self.expect_var_path("ushort_max", type="unsigned short", value="65535")

        self.expect_var_path("int_min", type="int", value="-2147483648")
        self.expect_var_path("int_max", type="int", value="2147483647")
        self.expect_var_path("int_zero", type="int", value="0")
        self.expect_var_path("int_neg_one", type="int", value="-1")
        self.expect_var_path("uint_zero", type="unsigned int", value="0")
        self.expect_var_path("uint_max", type="unsigned int", value="4294967295")

        self.expect_var_path(
            "llong_min", type="long long", value="-9223372036854775808"
        )
        self.expect_var_path(
            "llong_max", type="long long", value="9223372036854775807"
        )
        self.expect_var_path("llong_zero", type="long long", value="0")
        self.expect_var_path("llong_neg_one", type="long long", value="-1")
        self.expect_var_path("ullong_zero", type="unsigned long long", value="0")
        self.expect_var_path(
            "ullong_max",
            type="unsigned long long",
            value="18446744073709551615",
        )

        # Spot-check a few edge values through the expression evaluator too.
        self.expect_expr("int_min", result_type="int", result_value="-2147483648")
        self.expect_expr("int_max", result_type="int", result_value="2147483647")
        self.expect_expr(
            "llong_min", result_type="long long", result_value="-9223372036854775808"
        )
        self.expect_expr(
            "ullong_max",
            result_type="unsigned long long",
            result_value="18446744073709551615",
        )
