%feature("docstring",
"Represents a plan for the execution control of a given thread.

A thread plan describes what a thread should do until it stops again: run to an
address, step over a range of instructions, step out of a frame. LLDB implements
all of its stepping with thread plans, and the plans of a thread form a stack, so
a plan can push other plans it needs to reach its goal.

This class is mostly interesting for implementing a stepping algorithm in Python.
A scripted thread plan is a Python class that LLDB drives through its
``should_stop``, ``should_step`` and ``explains_stop`` methods, and it uses the
``QueueThreadPlanFor*`` functions of this class to have LLDB do the actual
stepping. See :doc:`/use/python-reference` for the interface such a class has to
implement, and `SBThread.StepUsingScriptedThreadPlan` for how it is started.

See also :py:class:`SBThread` and :py:class:`SBFrame`."
) lldb::SBThreadPlan;

%feature("docstring",
"Returns whether this object refers to a thread plan."
) lldb::SBThreadPlan::IsValid;

%feature("docstring",
"Resets this object to an invalid thread plan."
) lldb::SBThreadPlan::Clear;

%feature("docstring",
"Returns why the thread stopped as one of the ``lldb.eStopReason*``
enumerators.

See `SBThread.GetStopReason`."
) lldb::SBThreadPlan::GetStopReason;

%feature("docstring",
"Returns the `SBThread` this plan belongs to."
) lldb::SBThreadPlan::GetThread;

%feature("docstring",
"Writes a description of this thread plan into the given `SBStream`."
) lldb::SBThreadPlan::GetDescription;

%feature("docstring",
"Marks this plan as completed.

A scripted thread plan calls this when it reached its goal; ``success`` says
whether it did so successfully. Once a plan is complete LLDB pops it off the
plan stack of the thread."
) lldb::SBThreadPlan::SetPlanComplete;

%feature("docstring",
"Returns whether this plan reached its goal, see
`SBThreadPlan.SetPlanComplete`."
) lldb::SBThreadPlan::IsPlanComplete;

%feature("docstring",
"Returns whether this plan can no longer do anything useful.

A plan becomes stale when the state it depends on is gone, for example because
the frame it wanted to step out of has already returned."
) lldb::SBThreadPlan::IsPlanStale;

%feature("docstring",
"Queues a plan that steps over the given range of addresses.

Stepping over means that calls in the range are run to completion instead of
being stepped into. Returns the new plan, which becomes the plan LLDB runs
next::

    def should_step(self):
        return True

    def __init__(self, thread_plan, args):
        self.plan = thread_plan.QueueThreadPlanForStepOverRange(start_address, size)
"
) lldb::SBThreadPlan::QueueThreadPlanForStepOverRange;

%feature("docstring",
"Queues a plan that steps through the given range of addresses.

Unlike `SBThreadPlan.QueueThreadPlanForStepOverRange` this steps into the
functions that are called from the range. Returns the new plan."
) lldb::SBThreadPlan::QueueThreadPlanForStepInRange;

%feature("docstring",
"Queues a plan that runs until the given frame returns.

``frame_idx_to_step_to`` is the index of the frame that should be returned to and
``first_insn`` says whether stepping stops at the first instruction of that frame
instead of at the next source line. Returns the new plan."
) lldb::SBThreadPlan::QueueThreadPlanForStepOut;

%feature("docstring",
"Queues a plan that executes a single machine instruction.

If ``step_over`` is ``True`` a call instruction is stepped over instead of being
stepped into. Returns the new plan."
) lldb::SBThreadPlan::QueueThreadPlanForStepSingleInstruction;

%feature("docstring",
"Queues a plan that runs the thread to the given `SBAddress`.

Returns the new plan. Note that if the address is never reached the thread does
not stop on its own."
) lldb::SBThreadPlan::QueueThreadPlanForRunToAddress;

%feature("docstring",
"Queues another scripted thread plan.

``script_class_name`` is the name of a Python class that implements a thread plan
and ``args_data`` an `SBStructuredData` that is passed to its constructor. This
is how a scripted plan can delegate part of its work to another one. Returns the
new plan."
) lldb::SBThreadPlan::QueueThreadPlanForStepScripted;

%feature("docstring", "
    Get the number of words associated with the stop reason.
    See also `SBThreadPlan.GetStopReasonDataAtIndex`."
) lldb::SBThreadPlan::GetStopReasonDataCount;

%feature("docstring", "
    Get information associated with a stop reason.

    Breakpoint stop reasons will have data that consists of pairs of
    breakpoint IDs followed by the breakpoint location IDs (they always come
    in pairs).

    Stop Reason              Count Data Type
    ======================== ===== =========================================
    eStopReasonNone          0
    eStopReasonTrace         0
    eStopReasonBreakpoint    N     duple: {breakpoint id, location id}
    eStopReasonWatchpoint    1     watchpoint id
    eStopReasonSignal        1     unix signal number
    eStopReasonException     N     exception data
    eStopReasonExec          0
    eStopReasonFork          1     pid of the child process
    eStopReasonVFork         1     pid of the child process
    eStopReasonVForkDone     0
    eStopReasonPlanComplete  0"
) lldb::SBThreadPlan::GetStopReasonDataAtIndex;

%feature("docstring", "Return whether this plan will ask to stop other threads when it runs."
) lldb::SBThreadPlan::GetStopOthers;

%feature("docstring", "Set whether this plan will ask to stop other threads when it runs."
) lldb::SBThreadPlan::SetStopOthers;
