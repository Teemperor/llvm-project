import lldb
from lldbsuite.test.decorators import *
import lldbsuite.test.lldbtest as lldbtest
import lldbsuite.test.lldbutil as lldbutil


class TestSwiftDynamicTypeInvalidClassName(lldbtest.TestBase):
    @skipEmbeddedSwift
    @skipUnlessObjCInterop
    @swiftTest
    def test(self):
        """Test dynamic type resolution of a class whose name is not a valid
        identifier.

        Names of types that are reconstructed from the inferior's memory can be
        arbitrary bytes, for example when resolving the dynamic type of a
        variable that isn't initialized yet. Remangling such a name used to
        abort LLDB: an empty name produces a non-canonical mangled name, and a
        name that isn't valid UTF-8 cannot be punycode-encoded."""

        self.build()
        filespec = lldb.SBFileSpec("main.swift")
        lldbutil.run_to_source_breakpoint(self, "break here", filespec)

        # There is no dynamic type to be found, so the static type is used.
        self.expect("frame variable invalid_utf8_name", substrs=["(a.C) invalid_utf8_name"])
        self.expect("frame variable empty_name", substrs=["(a.C) empty_name"])
        self.expect("expression -- invalid_utf8_name", substrs=["(a.C)"])
        self.expect("expression -- empty_name", substrs=["(a.C)"])
