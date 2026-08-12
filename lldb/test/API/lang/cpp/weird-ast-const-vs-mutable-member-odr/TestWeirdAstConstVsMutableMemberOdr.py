"""
Test LLDB's behaviour when a struct's mutable-qualifier on a field
disagrees between two definitions merged into the scratch AST: the main
executable's 'Counter' has 'mutable int hits' and a 'bump() const' method
that increments it (well-defined C++, since 'hits' is mutable); the dylib's
'Counter' has a plain (non-mutable) 'int hits' and a 'bump() const' that is
a no-op and never touches 'hits' (so it compiles too, even though 'hits'
isn't mutable there).

This is a genuine ODR violation: both TUs define a type named 'Counter'
with identical layout (a single 'int' field named 'hits') and an
identically-mangled 'Counter::bump() const' method, but disagree on whether
'hits' is mutable. Clang's ASTImporter does not reconcile mutable-qualifier
mismatches when structurally unifying the two 'hits' FieldDecls into one
canonical RecordDecl - it just keeps whichever definition's FieldDecl it
saw first. The two (incompatibly-behaving, identically-mangled)
'Counter::bump() const' CXXMethodDecls get merged into a single
CXXMethodDecl carrying two AsmLabelAttrs, one pointing at each module's
address for the real, distinct machine code.

Taking a pointer-to-member '&Counter::bump' against that merged
CXXMethodDecl then dispatches to whichever definition's address was
attached first - which may be the main executable's 'hits++' version, even
when called through the dylib's genuinely-const, read-only-allocated
'Counter' object. That writes through what should be frozen, read-only
storage and crashes the *inferior* (not LLDB itself) with EXC_BAD_ACCESS -
LLDB catches this gracefully, reports it as an interrupted expression, and
the target process remains fully alive and debuggable afterwards.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstConstVsMutableMemberOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each module's own 'Counter' can be printed and its own 'bump()'
        called on its own object just fine, as long as the *other*
        conflicting 'Counter' hasn't already been imported into the shared
        per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr(
            "counterFromDylib.hits", result_type="const int", result_value="42"
        )

        # Calling the dylib's own no-op bump() through its own (correctly
        # dispatched, by-address) definition is harmless.
        self.expect_expr(
            "counterFromDylib.bump(), counterFromDylib.hits",
            result_type="const int",
            result_value="42",
        )

    @expectedFailureAll(
        bugnumber="ASTImporter merges two ODR-conflicting 'Counter' definitions "
        "that disagree on whether 'hits' is mutable into a single RecordDecl, "
        "silently keeping whichever FieldDecl (mutable or not) it saw first, "
        "and merges both TUs' identically-mangled 'Counter::bump() const' "
        "into a single CXXMethodDecl carrying two AsmLabelAttrs (one per "
        "module's real address). Forming '&Counter::bump' against that "
        "merged CXXMethodDecl and calling it through the dylib's genuinely "
        "const, read-only-allocated Counter object dispatches to whichever "
        "definition's address was attached first (often the main "
        "executable's 'hits++' version), writing through read-only storage "
        "and crashing the inferior with EXC_BAD_ACCESS. LLDB itself does not "
        "crash - it catches this gracefully and the target remains alive -  "
        "but this is still a real correctness bug: silently invoking the "
        "wrong module's method body against a mismatched object."
    )
    def test_both_together_pointer_to_member_crashes_inferior(self):
        """
        Pulls both conflicting 'Counter' definitions into the scratch AST
        (by referring to both global objects), forms a pointer-to-member to
        the merged 'bump' CXXMethodDecl, and calls it through the dylib's
        const object. This should not corrupt the object or crash the
        inferior; in practice it does crash the inferior (gracefully caught
        by LLDB) because the merged decl picks one module's machine code
        for calls that should have used the other's.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Pull both conflicting 'Counter' definitions into the scratch AST.
        # ('counterFromMain.hits' is 1, not 0, because main() already called
        # 'counterFromMain.bump()' once before reaching this breakpoint.)
        self.expect_expr(
            "counterFromDylib.hits", result_type="const int", result_value="42"
        )
        self.expect_expr("counterFromMain.hits", result_type="int", result_value="1")

        # Dispatching through a pointer-to-member formed against the merged
        # 'bump' CXXMethodDecl should behave like calling
        # 'counterFromDylib.bump()' directly (a harmless no-op that leaves
        # 'hits' at 42), not crash.
        self.expect_expr(
            "auto pFn = &Counter::bump; "
            "(counterFromDylib.*pFn)(), counterFromDylib.hits",
            result_type="const int",
            result_value="42",
        )
