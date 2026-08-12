"""
Test LLDB's handling of an ODR violation where two dylibs define classes
with the same names ('GrandBase', 'Mid1', 'Mid2', 'Derived') but with
fundamentally different multiple-inheritance diamond layouts: one dylib
uses virtual inheritance (a single shared GrandBase subobject), the other
uses non-virtual inheritance (two separate GrandBase subobjects). This
exercises LLDB's ASTImporter/TypeSystemClang/DWARFASTParserClang
machinery and the per-target shared scratch AST context under a
virtual-base-offset vs. non-virtual multiple-inheritance ODR conflict.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstVptrMultipleInheritanceDiamondOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each dylib's 'Derived' can be cast to 'GrandBase*' and have its
        'gb' field read just fine on its own, as long as the *other*
        dylib's conflicting 'Derived' (with the incompatible
        virtual/non-virtual diamond layout) hasn't also been imported
        into the shared per-target scratch AST context yet.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "entry", lldb.SBFileSpec("main.cpp"))

        # DylibA: Mid1/Mid2 virtually inherit GrandBase, so there is only
        # one (shared) GrandBase subobject and an unqualified cast to
        # 'GrandBase*' is unambiguous.
        self.expect("expr ((GrandBase*)gDerivedA)->f()")
        self.expect_expr(
            "((GrandBase*)gDerivedA)->gb", result_type="int", result_value="100"
        )

        # DylibB: Mid1/Mid2 non-virtually inherit GrandBase, so there are
        # two separate GrandBase subobjects and an unqualified cast to
        # 'GrandBase*' is ambiguous -- this is expected to fail to parse,
        # not crash.
        self.expect(
            "expr ((GrandBase*)gDerivedB)->gb",
            error=True,
            substrs=["ambiguous conversion from derived class"],
        )

        # Disambiguating via an intermediate cast to read the field works.
        # (Calling the virtual function 'f()' through such an intermediate
        # cast is exercised separately in test_both_together, since that
        # can crash the inferior process -- see the bug note there.)
        self.expect_expr(
            "((GrandBase*)(Mid1*)gDerivedB)->gb",
            result_type="int",
            result_value="200",
        )
        self.expect_expr(
            "((GrandBase*)(Mid2*)gDerivedB)->gb",
            result_type="int",
            result_value="201",
        )

    @expectedFailureAll(
        bugnumber="Once DylibA's virtually-inherited 'GrandBase'/'Mid1'/"
        "'Mid2'/'Derived' has been imported into the scratch AST context, "
        "importing DylibB's non-virtually-inherited same-named classes "
        "too (via a cast disambiguating the otherwise-ambiguous 'Mid1*'/"
        "'Mid2*' -> 'GrandBase*' conversion) and calling the virtual "
        "function 'f()' through that cast chain can make LLDB compute a "
        "this-pointer/vtable-slot address using virtual-base-offset (VTT) "
        "layout assumptions left over from DylibA's virtually-inherited "
        "'GrandBase', even though DylibB's object has no virtual base at "
        "all. Dereferencing the resulting bogus function pointer crashes "
        "the inferior process with EXC_BAD_ACCESS (caught by LLDB's "
        "expression evaluator) instead of returning a well-formed result. "
        "This only reproduces some of the time, since the exact garbage "
        "address computed depends on ASLR/heap layout"
    )
    def test_both_together(self):
        """
        Tests LLDB's expression evaluator against two dylibs whose
        'Derived' hierarchies share every class name but disagree on
        whether GrandBase is inherited virtually (DylibA, one shared
        GrandBase subobject) or non-virtually (DylibB, two separate
        GrandBase subobjects).

        Evaluating explicit-cast expressions for *both* dylibs' objects,
        including a call to the virtual function 'f()' through the cast
        chain, forces LLDB to import and reconcile both conflicting
        definitions of 'GrandBase'/'Mid1'/'Mid2'/'Derived' into the shared
        scratch AST context, and then resolve a virtual-base-offset (or
        non-virtual base) lookup and a virtual-function call against
        them. This can crash the inferior process outright when the JIT'd
        expression dereferences a bogus vtable-derived function pointer,
        rather than merely returning a wrong-but-well-formed value.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(self, "entry", lldb.SBFileSpec("main.cpp"))

        # Import DylibA's virtually-inherited 'Derived' into the scratch
        # AST context first.
        self.expect_expr(
            "((GrandBase*)gDerivedA)->gb", result_type="int", result_value="100"
        )

        # Now import DylibB's non-virtually-inherited 'Derived'/'Mid1' too,
        # and call a virtual function through the resulting cast chain.
        # This should just invoke GrandBase::f() (a no-op) without
        # crashing the inferior process.
        self.expect("expr ((GrandBase*)(Mid1*)gDerivedB)->f()")
        self.expect_expr(
            "((GrandBase*)(Mid1*)gDerivedB)->gb",
            result_type="int",
            result_value="200",
        )
