"""
Exercise TypeSystemClike::GetRuntimeCompletedObjCType via the child-enumeration
*Impl methods (GetNumChildrenImpl/GetChildCompilerTypeAtIndexImpl), which --
unlike CreateRuntimeObjCInterface's other callers (e.g.
ClikeExpressionDeclMap::LookupType) -- call into it while still holding this
TypeSystemClike instance's own lock (see the comment on
GetRuntimeCompletedObjCType). `object`'s ivars are only known to the ObjC
runtime (HiddenClass.m is compiled with -g0), so expanding them forces exactly
that redirect; blocking in a syscall before interrupting (like
commands/expression/expr-in-syscall's TestExpressionInSyscall) keeps the ObjC
runtime's isa-to-descriptor map cold, so completion has to build it for the
first time right here, from inside that already-locked call.

Note this deliberately goes through GetNumChildren/GetChildAtIndex (what
`frame variable -P1`/Xcode's variables view use), not GetChildMemberWithName
(`object->foo`): GetIndexOfChildMemberWithName takes no ExecutionContext in
the shared TypeSystem API, so as the very first query on a fresh SBValue it
cannot resolve a process to hand the ObjC runtime, and GetRuntimeCompletedObjCType
falls back to m_last_seen_process_wp, which nothing has warmed yet. That is a
narrow, separate, pre-existing gap (also visible as
TestHiddenIvars.test_frame_variable_across_modules's rdar://18683637
expectedFailure) -- not the locking hazard this test targets -- so it is not
exercised here.
"""

import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil


class RuntimeCompletionInSyscallTestCase(TestBase):
    @skipUnlessDarwin
    @expectedFailureNetBSD
    def test_frame_variable_children(self):
        self.build()

        # Stop once `object` is actually initialized (a plain source
        # breakpoint -- resolving it does not touch the ObjC runtime), so the
        # interrupt below cannot land before the assignment and see `object`
        # still NULL.
        target, process, thread, bkpt = lldbutil.run_to_source_breakpoint(
            self, "// breakpoint1", lldb.SBFileSpec("main.mm")
        )
        target.BreakpointDelete(bkpt.GetID())

        listener = self.dbg.GetListener()
        self.dbg.SetAsync(True)
        process.Continue()

        event = lldb.SBEvent()
        # Give the child enough time to reach the syscall, while clearing out
        # all the pending events. The last WaitForEvent call times out after
        # 2 seconds.
        while listener.WaitForEvent(2, event):
            pass

        self.assertEqual(process.GetState(), lldb.eStateRunning, "Process is running")

        process.SendAsyncInterrupt()
        while listener.WaitForEvent(2, event):
            pass

        self.assertEqual(process.GetState(), lldb.eStateStopped, PROCESS_STOPPED)

        # `object`'s static type (HiddenClass *) comes from main.mm's debug
        # info; its ivars do not (HiddenClass.m is -g0), so enumerating them
        # here -- via GetNumChildren/GetChildAtIndex, NOT via an evaluated
        # expression -- is what reaches GetRuntimeCompletedObjCType from
        # GetNumChildrenImpl/GetChildCompilerTypeAtIndexImpl while those
        # methods' own TypeSystemClike lock is held. A global (rather than a
        # frame-local), so the lookup does not depend on which thread/frame
        # the interrupt happens to land the debugger's selection on -- likely
        # somewhere inside the sleep syscall, not `main`.
        object_val = target.FindFirstGlobalVariable("object")
        self.assertTrue(object_val.IsValid())
        self.assertEqual(object_val.GetNumChildren(), 3)

        children_by_name = {
            object_val.GetChildAtIndex(i).GetName(): object_val.GetChildAtIndex(i)
            for i in range(object_val.GetNumChildren())
        }
        self.assertIn("foo", children_by_name, "foo (runtime-only ivar) not found")
        self.assertEqual(children_by_name["foo"].GetValueAsUnsigned(), 2)
        self.assertIn("bar", children_by_name, "bar (runtime-only ivar) not found")
        self.assertEqual(children_by_name["bar"].GetValueAsUnsigned(), 3)

        # Let the inferior fall out of its wait loop and exit.
        release_flag = target.FindFirstGlobalVariable("release_flag")
        release_flag.SetValueFromCString("1")
        process.Continue()
        while listener.WaitForEvent(10, event):
            new_state = lldb.SBProcess.GetStateFromEvent(event)
            if new_state == lldb.eStateExited:
                break
        self.assertState(process.GetState(), lldb.eStateExited)
