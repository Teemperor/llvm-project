import lldb
import os
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class TestCase(TestBase):
    def test(self):
        # Exercise the TypeSystemClike "frame variable" path directly instead of
        # the DIL evaluator.
        self.runCmd("settings set target.experimental.use-DIL false")

        # Capture DWARF type-completion messages so we can assert below that a
        # type only reachable through a pointer (`DoNotComplete`) is never
        # completed.
        log_file = os.path.join(self.getBuildDir(), "type-completion.log")
        self.runCmd("log enable -f '%s' dwarf comp" % log_file)
        self.addTearDownHook(lambda: self.runCmd("log disable dwarf comp"))

        self.build()
        lldbutil.run_to_source_breakpoint(
            self, "// break in method", lldb.SBFileSpec("main.cpp")
        )

        # Inside a member function: the implicit `this` and unqualified access
        # to a member variable (resolved through `this`).
        self.expect_expr("this", result_type="Container *")
        self.expect_expr("member_variable", result_value="99")
        # Calling a member function and a free function.
        self.expect_expr("this->funcCall()", result_type="SingleMember")
        self.expect_expr("globalFuncCall()", result_value="40")
        # Passing arguments into a member and a free function.
        self.expect_expr("this->addToMember(1)", result_value="100")
        self.expect_expr("globalAdd(3, 4)", result_value="7")

        # Continue to the breakpoint in main for the remaining checks.
        bkpt = self.target().BreakpointCreateBySourceRegex(
            "// break here", lldb.SBFileSpec("main.cpp")
        )
        self.assertGreater(bkpt.GetNumLocations(), 0)
        lldbutil.continue_to_breakpoint(self.process(), bkpt)

        self.expect_expr("local", result_value="20")

        # `DoNotComplete` is only ever referenced through a pointer, so a
        # *direct* GetCompleteType()/child-enumeration call on it must never
        # complete it (which would also load its member) -- verified via the
        # completion log below, captured before `outer.x`/`ptr->x` (both typed
        # `DoNotComplete *`) are ever touched.
        #
        # NOTE: the check is placed here, before those expressions run, rather
        # than at the end of the test: lldb's dynamic-value resolution (on by
        # default, target.prefer-dynamic-value) makes
        # ValueObject::IsPossibleDynamicType probe a pointer-to-class's pointee
        # for a vtable (isDynamicClass()) to decide whether the pointer itself
        # might need a dynamic type -- and answering that question requires
        # completing the pointee, same as TypeSystemClang's
        # GetCompleteType()->isDynamicClass() fallback (see
        # TypeSystemClike::IsPossibleDynamicType). Any later SBValue::GetError()/
        # GetChildAtIndex() call on such a value (e.g. the diagnostic dump
        # ValueCheck.check_value builds for every expect_expr(), even a
        # succeeding one) legitimately triggers that probe and completes
        # `DoNotComplete` as a side effect once its SBValue exists -- this is
        # correct dynamic-value behavior, not a laziness bug: it reproduces
        # identically with `frame variable` (no expression evaluator involved
        # at all) and under TypeSystemClang.
        self.runCmd("log disable dwarf comp")
        with open(log_file, "r") as f:
            completion_log = f.read()
        self.assertNotIn(
            "DoNotComplete",
            completion_log,
            "DoNotComplete was completed but should have stayed a forward "
            "declaration:\n" + completion_log,
        )

        # Records, inheritance and pointers.
        self.expect_expr("outer.m.i", result_value="4")
        # `Outer::x` is a `DoNotComplete *`. Referencing it (as a field type and
        # as the return type of `Outer::funcCall2`) does not itself complete
        # `DoNotComplete` (see the completion-log check above); it may still get
        # completed as a side effect of dynamic-value resolution once its
        # SBValue's children/error are queried (e.g. by the test harness's own
        # diagnostic-message construction) -- see the note above.
        self.expect_expr("outer.x", result_type="DoNotComplete *")
        self.expect_expr("ptr->x", result_type="DoNotComplete *")
        self.expect_expr("ptr", result_type="Outer *")
        self.expect_expr("*ptr", result_type="Outer")

        # Reference types collapse to the referenced value in an expression
        # result (unlike `frame variable`, which preserves the `&`/`&&` type):
        # `ref` evaluates to the referent `int`, etc.
        self.expect_expr("ref", result_type="int", result_value="99")
        self.expect_expr("rref", result_type="int", result_value="123")
        self.expect_expr("outer_ref", result_type="Outer")
        # References are transparent: members are reachable directly.
        self.expect_expr("outer_ref.m.i", result_value="4")
        self.expect_expr("outer_ref.x", result_type="DoNotComplete *")

        # Class templates: the instantiation is a normal record type whose name
        # includes the template arguments.
        self.expect_expr("wrapper", result_type="Wrapper<int>")
        self.expect_expr("wrapper.value", result_value="7")
        self.expect_expr("wrapper.tag", result_value="-1")

        # Non-type (value) template parameters: the instantiation name embeds
        # the value and the class is otherwise a normal record.
        self.expect_expr("fixed", result_type="FixedArray<3>")
        self.expect_expr("fixed.size", result_value="3")
        self.expect_expr("fixed.data[1]", result_value="20")

        # Typedefs keep their alias name but behave like the aliased type.
        self.expect_expr("my_int", result_type="MyInt", result_value="55")

        # const-qualified types.
        self.expect_expr("const_int", result_type="const int", result_value="42")

        # Enumerations: values are shown as their enumerator names.
        self.expect_expr("color", result_type="Color", result_value="Green")
        self.expect_expr("fruit", result_type="Fruit", result_value="Banana")

        # Unions: all members share the same storage (offset 0).
        self.expect_expr("number", result_type="Number")
        self.expect_expr("number.i", result_value="65")

        # Standard-library containers. These generate large, deeply-nested
        # template types (typedefs, pointers, base classes, unions, bitfields,
        # template arguments and nested types), so exercising their data
        # formatters end-to-end validates broad type-system coverage.
        # (Indexing via operator[] needs member-function call support, which
        # TypeSystemClike doesn't model yet; frame-variable indexing is covered by
        # TestCppSmokeTest.)
        self.expect_expr("str", result_summary='"hello"')
        self.expect_expr("vec", result_summary="size=3")
        self.expect_expr("tree_map", result_summary="size=2")
        self.expect_expr("hash_map", result_summary="size=1")
