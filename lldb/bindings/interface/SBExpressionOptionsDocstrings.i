%feature("docstring",
"A container for options to use when evaluating expressions.

Pass one to `SBFrame.EvaluateExpression`, `SBTarget.EvaluateExpression` or
`SBValue.EvaluateExpression` to control how the expression is run. The most
frequently used options say what may happen while the expression executes: how
long it may take, whether it may hit breakpoints and what happens if it
crashes::

    options = lldb.SBExpressionOptions()
    options.SetIgnoreBreakpoints(True)
    options.SetTimeoutInMicroSeconds(500000)   # Give up after half a second.
    options.SetTrapExceptions(False)
    value = frame.EvaluateExpression('compute_everything()', options)

Other options select the language of the expression
(`SBExpressionOptions.SetLanguage`), whether the result becomes a persistent
variable such as ``$0`` (`SBExpressionOptions.SetSuppressPersistentResult`) and
whether the expression defines new entities instead of computing a value
(`SBExpressionOptions.SetTopLevel`)."
) lldb::SBExpressionOptions;

%feature("docstring", "Sets whether to coerce the expression result to ObjC id type after evaluation."
) lldb::SBExpressionOptions::SetCoerceResultToId;

%feature("docstring", "Sets whether to unwind the expression stack on error."
) lldb::SBExpressionOptions::SetUnwindOnError;

%feature("docstring", "Sets whether to ignore breakpoint hits while running expressions."
) lldb::SBExpressionOptions::SetIgnoreBreakpoints;

%feature("docstring", "Sets whether to cast the expression result to its dynamic type."
) lldb::SBExpressionOptions::SetFetchDynamicValue;

%feature("docstring", "Sets the timeout in microseconds to run the expression for. If try all threads is set to true and the expression doesn't complete within the specified timeout, all threads will be resumed for the same timeout to see if the expression will finish."
) lldb::SBExpressionOptions::SetTimeoutInMicroSeconds;

%feature("docstring", "Sets the timeout in microseconds to run the expression on one thread before either timing out or trying all threads."
) lldb::SBExpressionOptions::SetOneThreadTimeoutInMicroSeconds;

%feature("docstring", "Sets whether to run all threads if the expression does not complete on one thread."
) lldb::SBExpressionOptions::SetTryAllThreads;

%feature("docstring", "Sets whether to stop other threads at all while running expressions.  If false, TryAllThreads does nothing."
) lldb::SBExpressionOptions::SetStopOthers;

%feature("docstring", "Sets whether to abort expression evaluation if an exception is thrown while executing.  Don't set this to false unless you know the function you are calling traps all exceptions itself."
) lldb::SBExpressionOptions::SetTrapExceptions;

%feature("docstring", "Sets the language that LLDB should assume the expression is written in"
) lldb::SBExpressionOptions::SetLanguage;

%feature("docstring", "Sets whether to generate debug information for the expression and also controls if a SBModule is generated."
) lldb::SBExpressionOptions::SetGenerateDebugInfo;

%feature("docstring", "Sets whether to produce a persistent result that can be used in future expressions."
) lldb::SBExpressionOptions::SetSuppressPersistentResult;

%feature("docstring", "Gets the prefix to use for this expression."
) lldb::SBExpressionOptions::GetPrefix;

%feature("docstring", "Sets the prefix to use for this expression. This prefix gets inserted after the 'target.expr-prefix' prefix contents, but before the wrapped expression function body."
) lldb::SBExpressionOptions::SetPrefix;

%feature("docstring", "Sets whether to auto-apply fix-it hints to the expression being evaluated."
) lldb::SBExpressionOptions::SetAutoApplyFixIts;

%feature("docstring", "Gets whether to auto-apply fix-it hints to an expression."
) lldb::SBExpressionOptions::GetAutoApplyFixIts;

%feature("docstring", "Sets how often LLDB should retry applying fix-its to an expression."
) lldb::SBExpressionOptions::SetRetriesWithFixIts;

%feature("docstring", "Gets how often LLDB will retry applying fix-its to an expression."
) lldb::SBExpressionOptions::GetRetriesWithFixIts;

%feature("docstring", "Gets whether to JIT an expression if it cannot be interpreted."
) lldb::SBExpressionOptions::GetAllowJIT;

%feature("docstring", "Sets whether to JIT an expression if it cannot be interpreted."
) lldb::SBExpressionOptions::SetAllowJIT;

%feature("docstring", "Sets language-plugin specific boolean option for expression evaluation. LLDB currently doesn't validate whether the option being set is understood by the expression evaluator."
) lldb::SBExpressionOptions::SetBooleanLanguageOption;

%feature("docstring", "Gets language-plugin specific boolean option for expression evaluation. LLDB currently doesn't validate whether the option being retrieved is one that is understood by the expression evaluator."
) lldb::SBExpressionOptions::GetBooleanLanguageOption;

%feature("docstring", "Gets whether the expression result is coerced to the ObjC id type after evaluation."
) lldb::SBExpressionOptions::GetCoerceResultToId;

%feature("docstring", "Gets whether the expression stack is unwound on error.

If this is disabled, the frames of a crashed expression are left on the stack so
they can be inspected."
) lldb::SBExpressionOptions::GetUnwindOnError;

%feature("docstring", "Gets whether breakpoint hits are ignored while running expressions."
) lldb::SBExpressionOptions::GetIgnoreBreakpoints;

%feature("docstring", "Gets whether the expression result is cast to its dynamic type.

The result is one of the ``lldb.eDynamic*`` enumerators, see
`SBValue.GetDynamicValue`."
) lldb::SBExpressionOptions::GetFetchDynamicValue;

%feature("docstring", "Gets the timeout in microseconds the expression may run for.

A timeout of ``0`` means the default timeout is used."
) lldb::SBExpressionOptions::GetTimeoutInMicroSeconds;

%feature("docstring", "Gets the timeout in microseconds the expression may run on one thread before all threads are resumed.

See `SBExpressionOptions.SetOneThreadTimeoutInMicroSeconds`."
) lldb::SBExpressionOptions::GetOneThreadTimeoutInMicroSeconds;

%feature("docstring", "Gets whether all threads are resumed if the expression does not complete on one thread."
) lldb::SBExpressionOptions::GetTryAllThreads;

%feature("docstring", "Gets whether other threads are stopped while the expression runs."
) lldb::SBExpressionOptions::GetStopOthers;

%feature("docstring", "Gets whether expression evaluation is aborted when an exception is thrown."
) lldb::SBExpressionOptions::GetTrapExceptions;

%feature("docstring", "Gets whether expression evaluation stops if the expression forks."
) lldb::SBExpressionOptions::GetStopOnFork;

%feature("docstring", "Sets whether expression evaluation stops if the expression forks.

Expressions that call ``fork`` or ``posix_spawn`` create a child process; with
this enabled LLDB stops when that happens instead of letting the child run."
) lldb::SBExpressionOptions::SetStopOnFork;

%feature("docstring", "Sets a callback that can cancel a running expression.

The callback is polled while the expression runs; returning ``True`` from it
aborts the evaluation. Use this to keep a user interface responsive during long
running expressions."
) lldb::SBExpressionOptions::SetCancelCallback;

%feature("docstring", "Gets whether debug information is generated for the expression."
) lldb::SBExpressionOptions::GetGenerateDebugInfo;

%feature("docstring", "Gets whether a persistent result variable is suppressed.

If persistent results are suppressed, the value of the expression is not
available as ``$0`` afterwards, see `SBValue.Persist`."
) lldb::SBExpressionOptions::GetSuppressPersistentResult;

%feature("docstring", "Gets whether the expression is parsed as top level code."
) lldb::SBExpressionOptions::GetTopLevel;

%feature("docstring", "Sets whether the expression is parsed as top level code.

Top level expressions are not wrapped in a function, so they can define new
functions, types and variables that later expressions can use, but they don't
produce a value."
) lldb::SBExpressionOptions::SetTopLevel;
