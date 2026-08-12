import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstTypesystemDumpMidCorruptionTestCase(TestBase):
    def test_each_alone(self):
        """
        Tests LLDB's handling of two *separate* dylibs that each define an
        abstract base class named 'Shared' with a single virtual method
        'go', but where DylibOne's 'go' is a genuine pure virtual (no
        body -- 'Shared' is a true abstract class there) while DylibTwo's
        'go' has a real, non-pure body (so DylibTwo's 'Shared' is an
        ordinary, instantiable polymorphic class). This is a deliberate
        ODR violation on the "is this class abstract" bit of an otherwise
        identically-shaped CXXRecordDecl.

        This baseline exercises each dylib's 'Shared' completely on its
        own -- including dumping the shared per-target scratch
        TypeSystem/ASTContext ('target dump typesystem') right after only
        one of the two conflicting 'Shared' definitions has been imported
        into it -- to confirm that referencing either dylib's 'Shared' in
        isolation, and dumping the scratch AST context at that point, both
        behave normally.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Dereferencing (rather than just null-checking the pointer)
        # forces LLDB to actually complete DylibOne's 'Shared' into the
        # scratch AST context.
        self.expect("expression *gSharedOne")
        self.expect("expression *gConcreteOne")
        self.expect("target dump typesystem", substrs=["Shared"])

    def test_both_together_then_dump(self):
        """
        Tests LLDB's behaviour when both dylibs' conflicting 'Shared'
        definitions (DylibOne's pure-virtual 'go' vs. DylibTwo's non-pure
        'go') are referenced from the same debug session, forcing LLDB's
        ASTImporter/TypeSystemClang machinery to import and reconcile two
        independent, ODR-conflicting completions of the same-named
        'Shared' RecordDecl into the shared per-target scratch
        ASTContext -- one completion whose DefinitionData says the class
        is abstract (and whose 'go' vtable slot is the "pure virtual
        called" trap), and one completion that says the exact opposite
        (and whose 'go' vtable slot is a real function).

        Immediately after both conflicting completions have been pulled
        in, this dumps the *entire* scratch TypeSystem/ASTContext with
        'target dump typesystem' (no filter), so LLDB's ASTDumper walks
        every Decl in the scratch context -- including the half-merged,
        internally-inconsistent 'Shared' RecordDecl -- and separately also
        dumps just the per-module ASTs for 'Shared'. This is the most
        promising place to look for the scratch AST's bookkeeping (or the
        recursive AST-printing machinery itself) getting confused by a
        record whose "abstract" bit and actual vtable contents disagree;
        at a minimum this should never crash LLDB, even if the merged
        type it prints ends up describing neither dylib's 'Shared'
        faithfully.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        # Pull DylibOne's abstract (pure-virtual 'go') 'Shared' into the
        # scratch AST context first. Dereferencing the pointer (rather
        # than just null-checking it) is what forces LLDB to actually
        # complete the type.
        self.expect("expression *gSharedOne")

        # Now also pull in DylibTwo's conflicting, non-abstract 'Shared'
        # (with a real body for 'go') from the same debug session. This
        # forces the ASTImporter to reconcile the two conflicting
        # CXXRecordDecls for 'Shared'.
        self.expect("expression *gSharedTwo")

        # Dump the per-module ASTs for 'Shared' from each module, so we
        # can see each dylib's own (still self-consistent) view of the
        # type before looking at the merged scratch context.
        self.expect("target modules dump ast --filter Shared")

        # Dump the *entire* scratch AST context (no --filter), which walks
        # and prints every Decl currently in it, including whatever
        # half-merged 'Shared' RecordDecl resulted from reconciling the
        # two conflicting completions above.
        self.expect("target dump typesystem", substrs=["Shared"])

        # Exercise a few more operations against the (potentially
        # inconsistent) merged type and dump again, in case corruption
        # only shows up after further use.
        self.expect("expression sizeof(*gSharedOne) + sizeof(*gSharedTwo)")
        self.expect("expression *gConcreteOne")
        self.expect("expression *gConcreteTwo")
        self.expect("target dump typesystem", substrs=["Shared"])

    @expectedFailureAll(
        bugnumber="Once DylibOne's genuinely-abstract 'Shared' (pure "
        "virtual 'go') has been imported into the scratch AST context, "
        "declaring a plain local 'Shared' and calling its 'go' method in "
        "a later, separate expression can resolve to the 'pure virtual "
        "called' vtable trap thunk left over from DylibOne's completion "
        "instead of being rejected as an abstract-class instantiation or "
        "resolving to DylibTwo's real, non-pure 'go' body, and aborts the "
        "inferior process (a well-formed EXC_BREAKPOINT trap caught by "
        "the expression evaluator) instead of returning a well-formed "
        "result"
    )
    def test_instantiate_merged_shared_calls_pure_virtual_trap(self):
        """
        After both conflicting 'Shared' completions have been imported
        into the scratch AST context (DylibOne's abstract/pure-virtual
        'go' and DylibTwo's concrete/non-pure 'go'), declare a plain,
        by-value local variable of the merged 'Shared' type in a *later*
        expression and call 'go' on it. In a standards-conforming single
        TU this would either be a hard compile error (you cannot
        instantiate an abstract class) or, if 'Shared' were genuinely
        non-abstract, a normal virtual call. Here, because the scratch
        AST's merged 'Shared' is internally inconsistent about which of
        those two worlds it lives in, the expression evaluator ends up
        constructing a 'Shared' and invoking a vtable slot that still
        contains DylibOne's "pure virtual called" trap thunk, crashing
        the *inferior* process (caught by LLDB as an EXC_BREAKPOINT
        expression interruption) rather than doing either of the two
        theoretically-sound things.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "main_entry", lldb.SBFileSpec("main.cpp")
        )

        self.expect("expression *gSharedOne")
        self.expect("expression *gSharedTwo")

        self.expect("expression Shared s; s.go();")
