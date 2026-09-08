"""
Tests calling a function that lives in an inline namespace, when the enclosing
namespace declares an overload of the same name.
"""

import lldb
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestInlineNamespaceFunctionCall(TestBase):
    def test(self):
        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break here", lldb.SBFileSpec("main.cpp")
        )

        # `w.s.v` makes the expression evaluator materialize the inline namespace
        # `outer::inner` while generating `Wrapper`'s member type; `fn` is only
        # looked up in it afterwards. Filing the generated function decl in an
        # inline namespace makes clang re-file it in the parent namespace too,
        # which sent LLDB looking for `outer::fn` from inside the lookup that was
        # still resolving `fn` -- recursing until the stack ran out.
        self.expect("expr w.s.v + outer::inner::fn(1)", substrs=["(int) $0 = 9"])

        # The overload in the inline namespace and the one in its parent both stay
        # callable, and by their own signatures.
        self.expect("expr outer::inner::fn(1)", substrs=["(int) $1 = 2"])
        self.expect("expr outer::fn(1)", substrs=["(int) $2 = 2"])
        self.expect("expr outer::fn(2.0)", substrs=["(int) $3 = 102"])
