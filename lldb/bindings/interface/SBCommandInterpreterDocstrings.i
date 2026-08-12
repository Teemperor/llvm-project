%feature("docstring",
"SBCommandInterpreter handles/interprets commands for lldb.

You get the command interpreter from the :py:class:`SBDebugger` instance
(`SBDebugger.GetCommandInterpreter`). It runs the LLDB command line commands
programmatically, which is often the shortest way to do something that has no
dedicated API, and it is how a script adds its own commands
(`SBCommandInterpreter.AddCommand`)::

    result = lldb.SBCommandReturnObject()
    debugger.GetCommandInterpreter().HandleCommand('breakpoint list', result)
    if result.Succeeded():
        print(result.GetOutput())

For example (from test/ python_api/interpreter/TestCommandInterpreterAPI.py),::

    def command_interpreter_api(self):
        '''Test the SBCommandInterpreter APIs.'''
        exe = os.path.join(os.getcwd(), 'a.out')

        # Create a target by the debugger.
        target = self.dbg.CreateTarget(exe)
        self.assertTrue(target, VALID_TARGET)

        # Retrieve the associated command interpreter from our debugger.
        ci = self.dbg.GetCommandInterpreter()
        self.assertTrue(ci, VALID_COMMAND_INTERPRETER)

        # Exercise some APIs....

        self.assertTrue(ci.HasCommands())
        self.assertTrue(ci.HasAliases())
        self.assertTrue(ci.HasAliasOptions())
        self.assertTrue(ci.CommandExists('breakpoint'))
        self.assertTrue(ci.CommandExists('target'))
        self.assertTrue(ci.CommandExists('platform'))
        self.assertTrue(ci.AliasExists('file'))
        self.assertTrue(ci.AliasExists('run'))
        self.assertTrue(ci.AliasExists('bt'))

        res = lldb.SBCommandReturnObject()
        ci.HandleCommand('breakpoint set -f main.c -l %d' % self.line, res)
        self.assertTrue(res.Succeeded())
        ci.HandleCommand('process launch', res)
        self.assertTrue(res.Succeeded())

        process = ci.GetProcess()
        self.assertTrue(process)

        ...

The HandleCommand() instance method takes two args: the command string and
an SBCommandReturnObject instance which encapsulates the result of command
execution.

See also :py:class:`SBCommandReturnObject` and :py:class:`SBCommand`.") lldb::SBCommandInterpreter;

%feature("docstring",
"Returns the name of a command argument type.

``arg_type`` is one of the ``lldb.eArgType*`` enumerators. This is the name that
appears in the help of a command, e.g. ``<breakpt-id>``."
) lldb::SBCommandInterpreter::GetArgumentTypeAsCString;

%feature("docstring",
"Returns the description of a command argument type.

``arg_type`` is one of the ``lldb.eArgType*`` enumerators."
) lldb::SBCommandInterpreter::GetArgumentDescriptionAsCString;

%feature("docstring",
"Returns whether the given `SBEvent` is a command interpreter event."
) lldb::SBCommandInterpreter::EventIsCommandInterpreterEvent;

%feature("docstring",
"Returns whether this object refers to a command interpreter."
) lldb::SBCommandInterpreter::IsValid;

%feature("docstring",
"Return whether a built-in command with the passed in
name or command path exists.

:param cmd: The command or command path to search for.
:return: ``True`` if the command exists, ``False`` otherwise."
) lldb::SBCommandInterpreter::CommandExists;

%feature("docstring",
"Return whether a user defined command with the passed in
name or command path exists.

User defined commands are the ones a script added with
`SBCommandInterpreter.AddCommand` or that were defined with
``command script add``.

:param cmd: The command or command path to search for.
:return: ``True`` if the command exists, ``False`` otherwise."
) lldb::SBCommandInterpreter::UserCommandExists;

%feature("docstring",
"Return whether the passed in name or command path
exists and is an alias to some other command.

:param cmd: The command or command path to search for.
:return: ``True`` if the alias exists, ``False`` otherwise."
) lldb::SBCommandInterpreter::AliasExists;

%feature("docstring",
"Returns the `SBBroadcaster` of this command interpreter.

Its events report for example that a command was executed or that the quit
command was used."
) lldb::SBCommandInterpreter::GetBroadcaster;

%feature("docstring",
"Returns the name of the broadcaster class of command interpreters.

Pass it to `SBListener.StartListeningForEventClass` to receive command
interpreter events."
) lldb::SBCommandInterpreter::GetBroadcasterClass;

%feature("docstring",
"Returns whether this interpreter has any commands."
) lldb::SBCommandInterpreter::HasCommands;

%feature("docstring",
"Returns whether this interpreter has any aliases."
) lldb::SBCommandInterpreter::HasAliases;

%feature("docstring",
"Returns whether any alias of this interpreter has options."
) lldb::SBCommandInterpreter::HasAliasOptions;

%feature("docstring",
"Returns whether the interpreter reads its commands from a terminal.

Commands can use this to decide whether asking the user something makes
sense."
) lldb::SBCommandInterpreter::IsInteractive;

%feature("docstring",
"Returns the `SBProcess` the commands of this interpreter operate on."
) lldb::SBCommandInterpreter::GetProcess;

%feature("docstring",
"Returns the `SBDebugger` this interpreter belongs to."
) lldb::SBCommandInterpreter::GetDebugger;

%feature("docstring",
"Adds a command that only exists to group subcommands.

Multiword commands are commands such as ``breakpoint``, which does nothing on its
own but has subcommands like ``breakpoint set``. Returns the new `SBCommand`, to
which subcommands can be added with `SBCommand.AddCommand`::

    group = interpreter.AddMultiwordCommand('mytool', 'My tool.')
    group.AddCommand('run', MyRunCommand(), 'Run my tool.')
"
) lldb::SBCommandInterpreter::AddMultiwordCommand;

%feature("docstring",
"Add a new command to the lldb::CommandInterpreter.

:param name: The name of the command.
:param impl: The handler of this command. In Python this is an object with a
    ``__call__(self, debugger, command, exe_ctx, result)`` method, or a class
    deriving from ``lldb.SBCommandPluginInterface`` that implements
    ``DoExecute``.
:param help: The general description to show as part of the help message of this
    command.
:param syntax: The syntax to show as part of the help message of this command.
    This could include a description of the different arguments and flags this
    command accepts.
:param auto_repeat_command: Autorepeating is triggered when the user presses
    Enter successively after executing a command. If ``None`` is provided, the
    previous exact command will be repeated. If ``''`` is provided, autorepeating
    is disabled. Otherwise, the provided string is used as a repeat command.
:return: An `SBCommand` representing the newly created command.

For example::

    class HelloCommand:
        def __call__(self, debugger, command, exe_ctx, result):
            result.AppendMessage('hello %s' % command)

    interpreter.AddCommand('hello', HelloCommand(), 'Say hello.')

The overloads that don\'t take ``auto_repeat_command`` create a command that does
not support autorepeat. See also the ``command script add`` command and
:doc:`/use/python-reference`."
) lldb::SBCommandInterpreter::AddCommand;

%feature("docstring",
"Reads the LLDB initialization file of the system-wide configuration directory.

The result of running the commands is written into the given
`SBCommandReturnObject`."
) lldb::SBCommandInterpreter::SourceInitFileInGlobalDirectory;

%feature("docstring",
"Reads the ``~/.lldbinit`` file of the current user.

The result of running the commands is written into the given
`SBCommandReturnObject`. ``is_repl`` says whether the REPL specific
initialization file should be used."
) lldb::SBCommandInterpreter::SourceInitFileInHomeDirectory;

%feature("docstring",
"Reads the ``.lldbinit`` file of the current working directory.

Note that LLDB only does this if the ``target.load-cwd-lldbinit`` setting allows
it. The result is written into the given `SBCommandReturnObject`."
) lldb::SBCommandInterpreter::SourceInitFileInCurrentWorkingDirectory;

%feature("docstring",
"Runs a command line command and returns its status.

The output and the errors of the command are written into ``result``, an
`SBCommandReturnObject`. ``add_to_history`` controls whether the command shows up
in the command history and the optional `SBExecutionContext` selects the target,
thread and frame the command operates on::

    result = lldb.SBCommandReturnObject()
    interpreter.HandleCommand('frame variable', result)
    print(result.GetOutput())

The return value is one of the ``lldb.eReturnStatus*`` enumerators; use
`SBCommandReturnObject.Succeeded` to just check for success."
) lldb::SBCommandInterpreter::HandleCommand;

%feature("docstring",
"Runs the commands in the given file.

``file`` is an `SBFileSpec`, ``options`` an
`SBCommandInterpreterRunOptions` that controls for example whether execution
stops at the first error, and the result of the commands is written into
``result``. This is what the ``command source`` command does."
) lldb::SBCommandInterpreter::HandleCommandsFromFile;

%feature("docstring",
"Returns the possible completions for a partially typed command.

``current_line`` is the line that was typed so far, ``cursor_pos`` the position of
the cursor in it, and ``match_start_point`` and ``max_return_elements`` restrict
which matches are returned. The matches are appended to ``matches``, an
`SBStringList`, whose first entry is the common prefix of all matches::

    matches = lldb.SBStringList()
    interpreter.HandleCompletion('br s', 4, 0, -1, matches)

See `SBCommandInterpreter.HandleCompletionWithDescriptions` to also get a
description for every match."
) lldb::SBCommandInterpreter::HandleCompletion;

%feature("docstring",
"Returns the possible completions for a partially typed command with
descriptions.

Behaves like `SBCommandInterpreter.HandleCompletion`, but also fills a second
`SBStringList` with a description for every match."
) lldb::SBCommandInterpreter::HandleCompletionWithDescriptions;

%feature("docstring",
"Returns whether an interrupt flag was raised either by the SBDebugger -
when the function is not running on the RunCommandInterpreter thread, or
by `SBCommandInterpreter.InterruptCommand` if it is.  If your code is doing
interruptible work, check this API periodically, and interrupt if it
returns true."
) lldb::SBCommandInterpreter::WasInterrupted;

%feature("docstring",
"Interrupts the command currently executing in the RunCommandInterpreter
thread.

:return: ``True`` if there was a command in progress to receive the interrupt,
    ``False`` if there\'s no command currently in flight."
) lldb::SBCommandInterpreter::InterruptCommand;

%feature("docstring",
"Replaces the implementation of an existing command with a callback.

Use this to change what a built-in command does. Returns whether a command with
that name existed."
) lldb::SBCommandInterpreter::SetCommandOverrideCallback;

%feature("docstring",
"Return true if the command interpreter is the active IO handler.

This indicates that any input coming into the debugger handles will
go to the command interpreter and will result in LLDB command line
commands being executed."
) lldb::SBCommandInterpreter::IsActive;

%feature("docstring",
"Get the string that needs to be written to the debugger stdin file
handle when a control character is typed.

Some GUI programs will intercept \"control + char\" sequences and want
to have them do what normally would happen when using a real
terminal, so this function allows GUI programs to emulate this
functionality.

:param ch: The character that was typed along with the control key.
:return: The string that should be written into the file handle that is
    feeding the input stream for the debugger, or ``None`` if there is
    no string for this control key."
) lldb::SBCommandInterpreter::GetIOHandlerControlSequence;

%feature("docstring",
"Returns whether the interpreter asks for confirmation before quitting."
) lldb::SBCommandInterpreter::GetPromptOnQuit;

%feature("docstring",
"Sets whether the interpreter asks for confirmation before quitting."
) lldb::SBCommandInterpreter::SetPromptOnQuit;

%feature("docstring",
"Sets whether the command interpreter should allow custom exit codes
for the \'quit\' command."
) lldb::SBCommandInterpreter::AllowExitCodeOnQuit;

%feature("docstring",
"Returns true if the user has called the \'quit\' command with a custom exit
code."
) lldb::SBCommandInterpreter::HasCustomQuitExitCode;

%feature("docstring",
"Returns the exit code that the user has specified when running the
\'quit\' command. Returns 0 if the user hasn\'t called \'quit\' at all or
without a custom exit code."
) lldb::SBCommandInterpreter::GetQuitStatus;

%feature("docstring",
"Resolve the command just as HandleCommand would, expanding abbreviations
and aliases.  If successful, ``result.GetOutput()`` has the full expansion."
) lldb::SBCommandInterpreter::ResolveCommand;

%feature("docstring",
"Returns statistics about the commands that were run as `SBStructuredData`.

See also `SBTarget.GetStatistics`."
) lldb::SBCommandInterpreter::GetStatistics;

%feature("docstring",
"Returns the commands that were handled so far as `SBStructuredData`.

The result is a list of dictionaries with the following keys:

* ``command`` (string): The command that was given by the user.
* ``commandName`` (string): The name of the executed command.
* ``commandArguments`` (string): The arguments of the executed command.
* ``output`` (string): The output of the command. Empty (``\"\"``) if no output.
* ``error`` (string): The error of the command. Empty (``\"\"``) if no error.
* ``durationInSeconds`` (float): The time it took to execute the command.
* ``timestampInEpochSeconds`` (int): The timestamp when the command was executed.

Turn on the ``interpreter.save-transcript`` setting for LLDB to populate this
list. Otherwise the list is empty."
) lldb::SBCommandInterpreter::GetTranscript;

%feature("docstring",
"Installs a callback that receives everything the interpreter prints.

Use this to capture or redirect the output of commands. The callback takes the
`SBCommandReturnObject` of the command that produced the output."
) lldb::SBCommandInterpreter::SetPrintCallback;

%feature("docstring",
"Represents a command of the command interpreter.

Commands are created with `SBCommandInterpreter.AddCommand` and
`SBCommandInterpreter.AddMultiwordCommand`. A multiword command can have
subcommands, which are added to it with `SBCommand.AddCommand`::

    group = interpreter.AddMultiwordCommand('mytool', 'My tool.')
    group.AddCommand('run', MyRunCommand(), 'Run my tool.')
"
) lldb::SBCommand;

%feature("docstring",
"Returns whether this object refers to a command."
) lldb::SBCommand::IsValid;

%feature("docstring",
"Returns the name of this command."
) lldb::SBCommand::GetName;

%feature("docstring",
"Returns the short help text of this command.

This is the text that ``help`` shows next to the command name."
) lldb::SBCommand::GetHelp;

%feature("docstring",
"Returns the long help text of this command.

This is the text that ``help <command>`` shows."
) lldb::SBCommand::GetHelpLong;

%feature("docstring",
"Sets the short help text of this command, see `SBCommand.GetHelp`."
) lldb::SBCommand::SetHelp;

%feature("docstring",
"Sets the long help text of this command, see
`SBCommand.GetHelpLong`."
) lldb::SBCommand::SetHelpLong;

%feature("docstring",
"Returns the flags of this command as a bit mask of the
``lldb.eCommandRequires*`` values.

The flags say what the command needs in order to run, for example
``lldb.eCommandRequiresTarget`` or ``lldb.eCommandProcessMustBePaused``. LLDB
checks them before the command runs and reports a fitting error if they are not
satisfied."
) lldb::SBCommand::GetFlags;

%feature("docstring",
"Sets the flags of this command, see `SBCommand.GetFlags`::

    command.SetFlags(lldb.eCommandRequiresProcess | lldb.eCommandProcessMustBePaused)
"
) lldb::SBCommand::SetFlags;

%feature("docstring",
"Adds a subcommand to this multiword command.

See `SBCommandInterpreter.AddCommand` for the parameters."
) lldb::SBCommand::AddCommand;

%feature("docstring",
"Adds a subcommand that itself only groups subcommands.

See `SBCommandInterpreter.AddMultiwordCommand`."
) lldb::SBCommand::AddMultiwordCommand;
