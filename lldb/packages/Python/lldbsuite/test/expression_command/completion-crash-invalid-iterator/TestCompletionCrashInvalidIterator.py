
import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbutil

class NamespaceBreakpointTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    def test_breakpoints_func_auto(self):
        self.build()

        # Create a target by the debugger.
        exe = self.getBuildArtifact("a.out")
        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)
        module_list = lldb.SBFileSpecList()
        module_list.Append(lldb.SBFileSpec(exe, False))
        cu_list = lldb.SBFileSpecList()
        # Set a breakpoint by name "func" which should pick up all functions
        # whose basename is "func"
        bp = target.BreakpointCreateByName(
            "S::S(E)", lldb.eFunctionNameTypeAuto, module_list, cu_list)

#        process = target.LaunchSimple(None, None, self.getBuildDir())
#        self.assertIsNotNone(process, PROCESS_IS_VALID)
#        thread = lldbutil.get_stopped_thread(process, lldb.eStopReasonBreakpoint)

#        self.assertIsNotNone(thread, "There should be a thread stopped "
#                             "due to breakpoint")

        self.dbg.HandleCommand("run")
        self.dbg.GetCommandInterpreter().HandleCompletion("expr ", len("expr "),
                                                          0, -1, lldb.SBStringList())
        assert False
