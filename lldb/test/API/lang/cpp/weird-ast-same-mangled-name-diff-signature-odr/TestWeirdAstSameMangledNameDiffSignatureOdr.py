"""
Test LLDB's handling of a mangled-name collision that is also an ODR
violation: the main executable and the 'plugin' dylib both define a
function under the exact same linker symbol (the real Itanium mangling
of "void impl::run()", i.e. _ZN4impl3runEv), but they declare and
define it with two incompatible C++ signatures:

  - main.cpp:   void impl::run()
  - plugin.cpp: int  impl::run(int, int)   [forced onto the same symbol
                                             name via asm("__ZN4impl3runEv")]

Both definitions are __attribute__((weak)), so the linker is free to
pick either one to satisfy every reference to that symbol -- including
references from the *other* module -- without complaining about a
duplicate symbol. This means the DWARF debug info for the two modules
permanently disagrees about the declared type of 'impl::run', even
though there is only a single function body backing the symbol at
runtime.

This is meant to stress SymbolFileDWARF's function-lookup-by-mangled-
name and the ASTImporter's decl-merging-by-linkage-name machinery: each
module's debug info hands back a FunctionDecl for 'impl::run' whose
FunctionProtoType (arity, parameter types, return type) disagrees with
the other module's FunctionDecl for the identical DW_AT_linkage_name.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class WeirdAstSameMangledNameDiffSignatureOdrTestCase(TestBase):
    def test_call_through_each_modules_own_signature(self):
        """
        Calling 'impl::run' with the argument list that matches whichever
        module's debug info LLDB actually resolves the call against
        should not crash, and should return a well-formed (if not
        necessarily "correct" in an ODR sense) result.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # This resolves against plugin.cpp's 'int impl::run(int, int)'
        # declaration.
        self.expect_expr("impl::run(1, 2)", result_type="int", result_value="3")

        # This resolves against main.cpp's 'void impl::run()'
        # declaration -- even though we are currently stopped inside the
        # dylib. Because both weak definitions share one linker symbol,
        # this ends up (mis)calling the same underlying function body
        # with zero arguments.
        self.expect_expr("impl::run()")

    def test_both_signatures_in_one_expression(self):
        """
        Referencing both of the ODR-violating declarations of
        'impl::run' from the two different modules within a single
        expression should not crash and should not corrupt the shared
        per-target scratch AST context.
        """
        self.build()

        lldbutil.run_to_source_breakpoint(
            self, "plugin_entry", lldb.SBFileSpec("plugin.cpp")
        )

        # Evaluating a call against each module's own declaration of
        # 'impl::run' is what makes DWARFASTParserClang materialize a
        # FunctionDecl for it in that module's own (per-module) Clang
        # AST. Do this for both modules first, so that the subsequent
        # 'target modules dump ast --filter run' actually has something
        # to show for each of them.
        self.expect_expr("impl::run(1, 2)", result_type="int", result_value="3")
        self.expect_expr("impl::run()")

        # Dump both modules' own view of 'impl::run': main.cpp's debug
        # info describes 'void impl::run()', while plugin.cpp's
        # describes 'int impl::run(int, int)' -- both under the same
        # DW_AT_linkage_name.
        self.expect(
            "target modules dump ast --filter run",
            substrs=["run", "void ()", "run", "int (int, int)"],
        )

        # Use both conflicting declarations in the same expression. This
        # forces LLDB to reason about 'impl::run' resolving to two
        # different FunctionProtoTypes at once.
        self.expect_expr(
            "impl::run(1, 2) + (impl::run(), 0)", result_type="int", result_value="3"
        )

        # The shared scratch AST context should still be in a usable
        # state afterwards.
        self.expect("target dump typesystem", substrs=["scratch Clang type system"])
