"""
Test LLDB's behaviour with 'Point' declared inside namespace 'A' but pulled
into unqualified lookup via a using-directive ('using namespace A;'),
rather than being reached with a direct namespace-qualified lookup
('A::Point'). This is done identically in the main executable and in a
dylib, but the two modules' 'A::Point' genuinely conflict (two fields vs.
three fields) -- an ODR violation.

Using-directives make DeclContext::lookup for an unqualified 'Point' chain
through the UsingDirectiveDecl that links a namespace/translation unit to
'A', rather than looking 'Point' up directly in 'A'. This test explores
whether reaching the same conflicting 'A::Point' via two different
using-directive chains (one per module) causes the ASTImporter/DWARF AST
parser to create inconsistent DeclContext-cache state -- e.g. importing the
two-field layout into the scratch AST context and then having the
three-field 'z' member access on a dylib-side 'Point' read/write out of
bounds of a stale cached RecordLayout, or trip an assertion in
ASTContext::getASTRecordLayout's size-consistency check.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstUsingDirectiveNamespaceMergeOdrTestCase(TestBase):
    def test_each_alone(self):
        """
        Each module's using-directive-reached 'Point' can be read on its
        own, before the other module's conflicting 'A::Point' has been
        imported into the scratch AST context.
        """
        self.build()

        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "Break in useA", lldb.SBFileSpec("main.cpp")
        )

        self.expect_expr("p.x", result_type="int", result_value="1")
        self.expect_expr("p.y", result_type="int", result_value="2")
        self.expect(
            "target modules dump ast --filter Point a.out",
            substrs=["struct Point", "x 'int'", "y 'int'"],
        )

        lldbutil.continue_to_source_breakpoint(
            self, process, "Break in useB", lldb.SBFileSpec("plugin.cpp")
        )

        self.expect_expr("p.x", result_type="int", result_value="1")
        self.expect_expr("p.y", result_type="int", result_value="2")
        self.expect_expr("p.z", result_type="int", result_value="3")
        self.expect(
            "target modules dump ast --filter Point libplugin.dylib",
            substrs=["struct Point", "x 'int'", "y 'int'", "z 'int'"],
        )

    def test_both_together(self):
        """
        Tests LLDB's behaviour when 'A::Point' -- reached unqualified via
        'using namespace A;' in both the main executable and a dylib --
        has two genuinely conflicting definitions: two fields ('x', 'y')
        in the executable, three fields ('x', 'y', 'z') in the dylib.

        This dumps each module's per-DWARF-module Clang AST for 'Point'
        (forcing the DWARFASTParserClang/ASTImporter machinery to actually
        parse and cache each module's using-directive-reached 'A::Point'
        independently), then evaluates 'p.z' in useB's frame followed by
        'p.x' in useA's frame within the same debug session, exercising
        whether resolving the same (conflicting) 'A::Point' through two
        different using-directive chains leaves any stale/shared
        DeclContext-lookup or RecordLayout-cache state behind that the
        other module's field access could observe.

        This should not crash LLDB, and each module's fields should be
        read using that module's own (correct) layout rather than a
        stale/incompatible cached one from the other module.
        """
        self.build()

        target, process, thread, bkpt_a = lldbutil.run_to_source_breakpoint(
            self, "Break in useA", lldb.SBFileSpec("main.cpp")
        )

        # Parse/cache the executable's using-directive-reached 'A::Point'
        # first.
        self.expect(
            "target modules dump ast --filter Point a.out",
            substrs=["struct Point", "x 'int'", "y 'int'"],
        )
        self.expect_expr("p.x", result_type="int", result_value="1")

        lldbutil.continue_to_source_breakpoint(
            self, process, "Break in useB", lldb.SBFileSpec("plugin.cpp")
        )

        # Now parse/cache the dylib's conflicting 'A::Point', reached via
        # its own, separate using-directive chain, forcing the
        # ASTImporter to reconcile it against the executable's 'A::Point'
        # already sitting in the scratch AST context.
        self.expect(
            "target modules dump ast --filter Point libplugin.dylib",
            substrs=["struct Point", "x 'int'", "y 'int'", "z 'int'"],
        )

        # Access the dylib's three-field 'Point' -- in particular the
        # 'z' field that the executable's conflicting 'Point' does not
        # have at all -- while stopped in useB's frame.
        self.expect_expr("p.z", result_type="int", result_value="3")
        self.expect_expr("p.x", result_type="int", result_value="1")
        self.expect_expr("p.y", result_type="int", result_value="2")

        # Dump the merged scratch AST context state after both
        # conflicting 'A::Point' definitions have been imported into it.
        self.expect("target dump typesystem")

        # Finally, re-select useA's stack frame and access its two-field
        # 'Point' again, after the dylib's three-field 'Point' has also
        # been imported into the same scratch AST context. 'useA' is
        # still live on the stack: it calls into the dylib's
        # 'plugin_entry' (which calls 'useB') from within its own body,
        # specifically so that its frame -- and its local 'Point p' --
        # is still around at this point. If the ASTImporter/DWARF parser
        # confused the two modules' 'A::Point' DeclContext-cache entries
        # with each other, this could read 'p.x'/'p.y' using the wrong
        # (three-field) layout, or otherwise misbehave.
        frame_a = None
        for f in thread.frames:
            if f.GetFunctionName() == "useA()":
                frame_a = f
                break
        self.assertIsNotNone(frame_a, "Could not find useA()'s frame on the stack")
        thread.SetSelectedFrame(frame_a.GetFrameID())

        self.expect_expr("p.x", result_type="int", result_value="1")
        self.expect_expr("p.y", result_type="int", result_value="2")

        self.expect(
            "target modules dump ast --filter Point a.out",
            substrs=["struct Point", "x 'int'", "y 'int'"],
        )
        self.expect(
            "target modules dump ast --filter Point libplugin.dylib",
            substrs=["struct Point", "x 'int'", "y 'int'", "z 'int'"],
        )
