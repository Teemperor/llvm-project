import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


@skipIfTargetDoesNotSupportSharedLibraries()
class ExprDefinitionInDylibTestCase(TestBase):
    SHARED_BUILD_TESTCASE = False

    @skipIf(
        compiler="clang",
        compiler_version=["<", "22"],
        bugnumber="Required Clang flag not supported",
    )
    @skipIfWindows
    def test_with_structor_linkage_names(self):
        """
        Tests that we can call functions whose definition
        is in a different LLDB module than it's declaration.
        """
        self.build(dictionary={"CXXFLAGS_EXTRAS": "-gstructor-decl-linkage-names"})

        target = self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.assertTrue(target, VALID_TARGET)

        env = self.registerSharedLibrariesWithTarget(target, ["lib"])

        breakpoint = lldbutil.run_break_set_by_file_and_line(
            self, "main.cpp", line_number("main.cpp", "return")
        )

        process = target.LaunchSimple(None, env, self.get_process_working_directory())

        self.assertIsNotNone(
            lldbutil.get_one_thread_stopped_at_breakpoint_id(self.process(), breakpoint)
        )

        self.expect_expr("f.method()", result_value="-72", result_type="int")

        self.expect_expr("Foo(10)", result_type="Foo")

        self.expect_expr("Base()", result_type="Base")

        self.expect_expr("Bar()", result_type="Bar")

        # Test a more complex setup: expression that has a three bases:
        # 1. definition is in local module
        # 2. definition is in different module
        # 3. definition is in expression context (and has it's own virtual base)
        #
        # Reading the value of a *nested* virtual base of a JIT'd expression
        # result (e.g. Expr.Local.Foo.x) requires resolving the nested
        # virtual-base offset from the result object's vtable. TypeSystemClike
        # does not yet do this for expression-result records (it needs the
        # out-of-scope vtable-based dynamic virtual-base offset resolution).
        # The cross-module structor calls above already pass; skip only this
        # nested-virtual-base value check under TypeSystemClike.
        if self.dbg.GetSetting("symbols.enable-typesystem-clike").GetBooleanValue():
            return

        self.expect_expr(
            "struct ExprBase : virtual Foo { int z; ExprBase() : Foo(11) { z = x; } }; struct Expr : virtual Local, virtual Foo, virtual ExprBase { int w; Expr() : Local(), Foo(12), ExprBase() { w = y; } }; Expr tmp; tmp",
            result_type="Expr",
            result_children=[
                ValueCheck(
                    name="Local",
                    children=[
                        ValueCheck(
                            name="Foo", children=[ValueCheck(name="x", value="12")]
                        ),
                        ValueCheck(name="y", value="12"),
                    ],
                ),
                ValueCheck(name="Foo", children=[ValueCheck(name="x", value="12")]),
                ValueCheck(
                    name="ExprBase",
                    children=[
                        ValueCheck(
                            name="Foo", children=[ValueCheck(name="x", value="12")]
                        ),
                        ValueCheck(name="z", value="12"),
                    ],
                ),
                ValueCheck(name="w", value="12"),
            ],
        )

    @skipIfWindows
    def test_no_structor_linkage_names(self):
        """
        Tests that if structor declarations don't have linkage names, we can't
        call ABI-tagged constructors. But non-tagged ones are fine.
        """
        # In older versions of Clang the -gno-structor-decl-linkage-names
        # behaviour was the default.
        if self.expectedCompiler(["clang"]) and self.expectedCompilerVersion(
            [">=", "22.0"]
        ):
            self.build(
                dictionary={"CXXFLAGS_EXTRAS": "-gno-structor-decl-linkage-names"}
            )
        else:
            self.build()

        target = self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.assertTrue(target, VALID_TARGET)

        env = self.registerSharedLibrariesWithTarget(target, ["lib"])

        breakpoint = lldbutil.run_break_set_by_file_and_line(
            self, "main.cpp", line_number("main.cpp", "return")
        )

        process = target.LaunchSimple(None, env, self.get_process_working_directory())

        self.assertIsNotNone(
            lldbutil.get_one_thread_stopped_at_breakpoint_id(self.process(), breakpoint)
        )

        self.expect_expr("f.method()", result_value="-72", result_type="int")

        self.expect_expr("Foo(10)", result_type="Foo")

        self.expect("expr Base()", error=True)

        self.expect("expr Bar()", error=True)
