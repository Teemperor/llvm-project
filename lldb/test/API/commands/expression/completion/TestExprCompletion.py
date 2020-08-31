"""
Test the lldb command line completion mechanism for the 'expr' command.
"""


import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbplatform
from lldbsuite.test import lldbutil

class CommandLineExprCompletionTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    NO_DEBUG_INFO_TESTCASE = True

    def test_expr_completion(self):
        self.build()
        self.main_source = "main.cpp"
        self.main_source_spec = lldb.SBFileSpec(self.main_source)
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))

        # Try the completion before we have a context to complete on.
        self.assert_no_completions('expr some_expr')
        self.assert_no_completions('expr ')
        self.assert_no_completions('expr f')

        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(self,
                                          '// Break here', self.main_source_spec)

        # Completing member functions
        self.assert_completions_contain('expr some_expr.FooNoArgs',
                                        ['some_expr.FooNoArgsBar()'])
        self.assert_completions_contain('expr some_expr.FooWithArgs',
                                        ['some_expr.FooWithArgsBar('])
        self.assert_completions_contain('expr some_expr.FooWithMultipleArgs',
                                        ['some_expr.FooWithMultipleArgsBar('])
        self.assert_completions_contain('expr some_expr.FooUnderscore',
                                        ['some_expr.FooUnderscoreBar_()'])
        self.assert_completions_contain('expr some_expr.FooNumbers',
                                        ['some_expr.FooNumbersBar1()'])
        self.assert_completions_contain('expr some_expr.StaticMemberMethod',
                                        ['some_expr.StaticMemberMethodBar()'])

        # Completing static functions
        self.assert_completions_contain('expr Expr::StaticMemberMethod',
                                        ['Expr::StaticMemberMethodBar()'])

        # Completing member variables
        self.assert_completions_contain('expr some_expr.MemberVariab',
                                        ['some_expr.MemberVariableBar'])

        # Multiple completions
        self.assert_completions_contain('expr some_expr.',
                                        ['some_expr.FooNumbersBar1()',
                                         'some_expr.FooUnderscoreBar_()',
                                         'some_expr.FooWithArgsBar(',
                                         'some_expr.MemberVariableBar'])

        self.assert_completions_contain('expr some_expr.Foo',
                                        ['some_expr.FooNumbersBar1()',
                                         'some_expr.FooUnderscoreBar_()',
                                         'some_expr.FooWithArgsBar('])

        self.assert_completions_contain('expr ',
                                        ['static_cast',
                                         'reinterpret_cast',
                                         'dynamic_cast'])

        self.assert_completions_contain('expr 1 + ',
                                        ['static_cast',
                                         'reinterpret_cast',
                                         'dynamic_cast'])

        # Completion expr without spaces
        # This is a bit awkward looking for the user, but that's how
        # the completion API works at the moment.
        self.assert_completions_contain('expr 1+',
                                        ['1+some_expr', "1+static_cast"])

        # Test with spaces
        self.assert_completions_contain('expr   some_expr .FooNoArgs',
                                        ['.FooNoArgsBar()'])
        self.assert_completions_contain('expr  some_expr .FooNoArgs',
                                        ['.FooNoArgsBar()'])
        self.assert_completions_contain('expr some_expr .FooNoArgs',
                                        ['.FooNoArgsBar()'])
        self.assert_completions_contain('expr some_expr. FooNoArgs',
                                        ['FooNoArgsBar()'])
        self.assert_completions_contain('expr some_expr . FooNoArgs',
                                        ['FooNoArgsBar()'])
        self.assert_completions_contain('expr Expr :: StaticMemberMethod',
                                        ['StaticMemberMethodBar()'])
        self.assert_completions_contain('expr Expr ::StaticMemberMethod',
                                        ['::StaticMemberMethodBar()'])
        self.assert_completions_contain('expr Expr:: StaticMemberMethod',
                                        ['StaticMemberMethodBar()'])

        # Test that string literals don't break our parsing logic.
        self.assert_completions_contain('expr const char *cstr = "some_e"; char c = *cst',
                                        ['*cstr'])
        self.assert_completions_contain('expr const char *cstr = "some_e" ; char c = *cst',
                                        ['*cstr'])

        # Completing inside double dash should do nothing.
        self.assert_no_completions("expr -i0 -- some_expr.", cursor_pos=10)
        self.assert_no_completions("expr -i0 -- some_expr.", cursor_pos=11)

        # Requesting completions inside an incomplete string doesn't provide any
        # completions.
        self.assert_no_completions('expr const char *cstr = "some_e')

        # Test with expr arguments
        self.assert_completions_contain('expr -i0 -- some_expr .FooNoArgs',
                                        ['.FooNoArgsBar()'])
        self.assert_completions_contain('expr  -i0 -- some_expr .FooNoArgs',
                                        ['.FooNoArgsBar()'])

        # Addrof and deref
        self.assert_completions_contain('expr (*(&some_expr)).FooNoArgs',
                                        ['(*(&some_expr)).FooNoArgsBar()'])
        self.assert_completions_contain('expr (*(&some_expr)) .FooNoArgs',
                                        ['.FooNoArgsBar()'])
        self.assert_completions_contain('expr (* (&some_expr)) .FooNoArgs',
                                        ['.FooNoArgsBar()'])
        self.assert_completions_contain('expr (* (& some_expr)) .FooNoArgs',
                                        ['.FooNoArgsBar()'])

        # Addrof and deref (part 2)
        self.assert_completions_contain('expr (&some_expr)->FooNoArgs',
                                        ['(&some_expr)->FooNoArgsBar()'])
        self.assert_completions_contain('expr (&some_expr) ->FooNoArgs',
                                        ['->FooNoArgsBar()'])
        self.assert_completions_contain('expr (&some_expr) -> FooNoArgs',
                                        ['FooNoArgsBar()'])
        self.assert_completions_contain('expr (&some_expr)-> FooNoArgs',
                                        ['FooNoArgsBar()'])

        # Builtin arg
        self.assert_completions_contain('expr static_ca',
                                        ['static_cast'])

        # From other files
        self.assert_completions_contain('expr fwd_decl_ptr->Hidden',
                                        ['fwd_decl_ptr->HiddenMemberName'])


        # Types
        self.assert_completions_contain('expr LongClassNa',
                                        ['LongClassName'])
        self.assert_completions_contain('expr LongNamespaceName::NestedCla',
                                        ['LongNamespaceName::NestedClass'])

        # Namespaces
        self.assert_completions_contain('expr LongNamespaceNa',
                                        ['LongNamespaceName::'])

        # Multiple arguments
        self.assert_completions_contain('expr &some_expr + &some_e',
                                       ['&some_expr'])
        self.assert_completions_contain('expr SomeLongVarNameWithCapitals + SomeLongVarName',
                                        ['SomeLongVarNameWithCapitals'])
        self.assert_completions_contain('expr SomeIntVar + SomeIntV',
                                        ['SomeIntVar'])

        # Multiple statements
        self.assert_completions_contain('expr long LocalVariable = 0; LocalVaria',
                                        ['LocalVariable'])

        # Custom Decls
        self.assert_completions_contain('expr auto l = [](int LeftHandSide, int bx){ return LeftHandS',
                                        ['LeftHandSide'])
        self.assert_completions_contain('expr struct LocalStruct { long MemberName; } ; LocalStruct S; S.Mem',
                                        ['S.MemberName'])

        # Completing function call arguments
        self.assert_completions_contain('expr some_expr.FooWithArgsBar(some_exp',
                                        ['some_expr.FooWithArgsBar(some_expr'])
        self.assert_completions_contain('expr some_expr.FooWithArgsBar(SomeIntV',
                                        ['some_expr.FooWithArgsBar(SomeIntVar'])
        self.assert_completions_contain('expr some_expr.FooWithMultipleArgsBar(SomeIntVar, SomeIntVa',
                                        ['SomeIntVar'])

        # Function return values
        self.assert_completions_contain('expr some_expr.Self().FooNoArgs',
                                        ['some_expr.Self().FooNoArgsBar()'])
        self.assert_completions_contain('expr some_expr.Self() .FooNoArgs',
                                        ['.FooNoArgsBar()'])
        self.assert_completions_contain('expr some_expr.Self(). FooNoArgs',
                                        ['FooNoArgsBar()'])

    def test_expr_completion_with_descriptions(self):
        self.build()
        self.main_source = "main.cpp"
        self.main_source_spec = lldb.SBFileSpec(self.main_source)
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))

        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(self,
                                          '// Break here', self.main_source_spec)

        self.check_completion_with_desc("expr ", [
            # builtin types have no description.
            ["int", ""],
            ["float", ""],
            # VarDecls have their type as description.
            ["some_expr", "Expr &"],
        ], enforce_order = True)
        self.check_completion_with_desc("expr some_expr.", [
            # Functions have their signature as description.
            ["some_expr.~Expr()", "inline ~Expr()"],
            ["some_expr.operator=(", "inline Expr &operator=(const Expr &)"],
            # FieldDecls have their type as description.
            ["some_expr.MemberVariableBar", "int"],
            ["some_expr.StaticMemberMethodBar()", "static int StaticMemberMethodBar()"],
            ["some_expr.Self()", "Expr &Self()"],
            ["some_expr.FooNoArgsBar()", "int FooNoArgsBar()"],
            ["some_expr.FooWithArgsBar(", "int FooWithArgsBar(int)"],
            ["some_expr.FooNumbersBar1()", "int FooNumbersBar1()"],
            ["some_expr.FooUnderscoreBar_()", "int FooUnderscoreBar_()"],
            ["some_expr.FooWithMultipleArgsBar(", "int FooWithMultipleArgsBar(int, int)"],
        ], enforce_order = True)
