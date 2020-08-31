"""
Test the lldb command line completion mechanism.
"""



import os
from multiprocessing import Process
import lldb
from lldbsuite.test.decorators import *
from lldbsuite.test.lldbtest import *
from lldbsuite.test import lldbplatform
from lldbsuite.test import lldbutil


class CommandLineCompletionTestCase(TestBase):

    mydir = TestBase.compute_mydir(__file__)

    NO_DEBUG_INFO_TESTCASE = True

    @classmethod
    def classCleanup(cls):
        """Cleanup the test byproducts."""
        try:
            os.remove("child_send.txt")
            os.remove("child_read.txt")
        except:
            pass

    def test_at(self):
        self.assert_completions_equal('at', ['attach'])

    def test_de(self):
        self.assert_completions_equal('de', ['detach'])

    def test_frame_variable(self):
        self.build()
        self.main_source = "main.cpp"
        self.main_source_spec = lldb.SBFileSpec(self.main_source)

        (target, process, thread, bkpt) = lldbutil.run_to_source_breakpoint(self,
                                          '// Break here', self.main_source_spec)
        self.assertEquals(process.GetState(), lldb.eStateStopped)

        # Since CommandInterpreter has been corrected to update the current execution
        # context at the beginning of HandleCompletion, we're here explicitly testing
        # the scenario where "frame var" is completed without any preceding commands.

        self.assert_completions_equal('frame variable fo', ["fooo"])
        self.assert_no_completions('frame variable fooo.')
        self.assert_no_completions('frame variable fooo.dd')

        self.assert_no_completions('frame variable ptr_fooo->')
        self.assert_no_completions('frame variable ptr_fooo->dd')

        self.assert_completions_equal('frame variable cont',
                                      ['container'])
        self.assert_completions_equal('frame variable container.',
                                      ['container.MemberVar'])
        self.assert_completions_equal('frame variable container.Mem',
                                      ['container.MemberVar'])

        self.assert_completions_equal('frame variable ptr_cont',
                                      ['ptr_container'])
        self.assert_completions_equal('frame variable ptr_container->',
                                      ['ptr_container->MemberVar'])
        self.assert_completions_equal('frame variable ptr_container->Mem',
                                      ['ptr_container->MemberVar'])

    def test_process_attach_dash_dash_con(self):
        """Test that 'process attach --con' completes to 'process attach --continue '."""
        self.assert_completions_equal("process attach --con", ["--continue"])

    def test_process_launch_arch(self):
        self.assert_completions_contain('process launch --arch ',
                                        ['mips',
                                        'arm64'])

    def test_process_load(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, '// Break here', lldb.SBFileSpec("main.cpp"))
        self.assert_completions_equal('process load Makef', ['Makefile'])

    @skipUnlessPlatform(["linux"])
    def test_process_unload(self):
        """Test the completion for "process unload <index>" """
        # This tab completion should not work without a running process.
        self.assert_no_completions('process unload ')

        self.build()
        lldbutil.run_to_source_breakpoint(self, '// Break here', lldb.SBFileSpec("main.cpp"))
        err = lldb.SBError()
        self.process().LoadImage(lldb.SBFileSpec(self.getBuildArtifact("libshared.so")), err)
        self.assertSuccess(err)

        self.assert_completions_contain('process unload ', ["0"])

        self.process().UnloadImage(0)
        # FIXME: This should be assert_no_completions.
        self.assert_completions_contain('process unload ', ["0"])

    def test_process_plugin_completion(self):
        subcommands = ['attach -P', 'connect -p', 'launch -p']

        for subcommand in subcommands:
            self.assert_completions_equal('process ' + subcommand + ' mac', ["mach-o-core"])

    def completions_contain_str(self, input, needle):
        match_strings = self.get_completion_list(input)
        found_needle = False
        for match in match_strings:
          if needle in match:
            found_needle = True
            break
        self.assertTrue(found_needle, "Returned completions: " + str(match_strings))

    @skipIfRemote
    def test_common_completion_process_pid_and_name(self):
        # The LLDB process itself and the process already attached to are both
        # ignored by the process discovery mechanism, thus we need a process known
        # to us here.
        self.build()
        server = self.spawnSubprocess(
            self.getBuildArtifact("a.out"),
            ["-x"], # Arg "-x" makes the subprocess wait for input thus it won't be terminated too early
            install_remote=False)
        self.assertIsNotNone(server)
        pid = server.pid

        self.assert_completions_contain('process attach -p ', [str(pid)])
        self.assert_completions_contain('platform process attach -p ', [str(pid)])
        self.assert_completions_contain('platform process info ', [str(pid)])

        self.completions_contain_str('process attach -n ', "a.out")
        self.completions_contain_str('platform process attach -n ', "a.out")

    def test_process_signal(self):
        # The tab completion for "process signal"  won't work without a running process.
        self.assert_no_completions('process signal ')

        # Test with a running process.
        self.build()
        self.main_source = "main.cpp"
        self.main_source_spec = lldb.SBFileSpec(self.main_source)
        lldbutil.run_to_source_breakpoint(self, '// Break here', self.main_source_spec)

        self.assert_completions_contain('process signal ',
                                        ['SIGABRT',
                                        'SIGALRM'])

    def test_ambiguous_long_opt(self):
        self.assert_completions_equal('breakpoint modify --th',
                                      ['--thread-id',
                                      '--thread-index',
                                      '--thread-name'])

    def test_disassemble_dash_f(self):
        self.assert_completions_equal('disassemble -F ',
                                      ['default',
                                      'intel',
                                      'att'])

    def test_plugin_load(self):
        # Just completes files like the Makefile in the test directory.
        self.assert_completions_contain('plugin load ', ['Makefile'])

    def test_log_enable(self):
        self.assert_completions_equal('log enable ll', ['lldb'])
        self.assert_completions_equal('log enable dw', ['dwarf'])
        self.assert_completions_equal('log enable lldb al', ['all'])
        self.assert_completions_equal('log enable lldb sym', ['symbol'])

    def test_log_enable(self):
        self.assert_completions_equal('log disable ll', ['lldb'])
        self.assert_completions_equal('log disable dw', ['dwarf'])
        self.assert_completions_equal('log disable lldb al', ['all'])
        self.assert_completions_equal('log disable lldb sym', ['symbol'])

    def test_log_list(self):
        self.assert_completions_equal('log list ll', ['lldb'])
        self.assert_completions_equal('log list dw', ['dwarf'])
        self.assert_completions_equal('log list ll', ['lldb'])
        self.assert_completions_equal('log list lldb dwa', ['dwarf'])

    def test_quoted_command(self):
        self.assert_completions_equal('"set', ['settings'])

    def test_quoted_arg_with_quoted_command(self):
        self.assert_completions_equal('"settings" "repl', ['replace'])

    def test_quoted_arg_without_quoted_command(self):
        self.assert_completions_equal('settings "repl', ['replace'])

    def test_single_quote_command(self):
        self.assert_completions_equal("'set", ["settings"])

    def test_terminated_quote_command(self):
        # This should not crash, but we don't get any
        # reasonable completions from this.
        self.assert_completions_equal("'settings'", ["settings"])

    def test_process_launch_arch_arm(self):
        self.assert_completions_contain('process launch --arch arm', ['arm64'])

    def test_target_symbols_add_shlib(self):
        # Doesn't seem to work, but at least it shouldn't crash.
        self.assert_no_completions('target symbols add --shlib ')

    def test_log_file(self):
        # Complete in our source directory which contains a 'main.cpp' file.
        src_dir =  os.path.dirname(os.path.realpath(__file__)) + '/'
        self.assert_completions_contain('log enable lldb expr -f ' + src_dir,
                                        [os.path.join(src_dir, 'main.cpp')])

    def test_log_dir(self):
        # Complete our source directory.
        src_dir =  os.path.dirname(os.path.realpath(__file__))
        self.assert_completions_contain('log enable lldb expr -f ' + src_dir,
                                        [src_dir + os.sep])

    # <rdar://problem/11052829>
    def test_infinite_loop_while_completing(self):
        """Test that 'process print hello\' completes to itself and does not infinite loop."""
        self.assert_no_completions('process print hello\\')

    def test_watchpoint_co(self):
        self.assert_completions_equal('watchpoint co', ['command'])

    def test_watchpoint_command_space(self):
        self.assert_completions_equal('watchpoint command ',
                                      ['add', 'delete', 'list'])

    def test_watchpoint_command_a(self):
        self.assert_completions_equal('watchpoint command a',
                                      ["add"])

    def test_watchpoint_set_ex(self):
        self.assert_completions_equal('watchpoint set ex', ['expression'])

    def test_watchpoint_set_var(self):
        self.assert_completions_equal('watchpoint set var', ['variable'])

    def test_watchpoint_set_variable_foo(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, '// Break here', lldb.SBFileSpec("main.cpp"))
        self.assert_completions_equal('watchpoint set variable fo', ['fooo'])
        # Only complete the first argument.
        self.assert_no_completions('watchpoint set variable fooo ')

    def test_help_fi(self):
        self.assert_completions_contain( 'help fi', ['file', 'finish'])

    def test_help_watchpoint_s(self):
        self.assert_completions_contain('help watchpoint s', ['set'])

    def test_common_complete_watchpoint_ids(self):
        subcommands = ['enable', 'disable', 'delete', 'modify', 'ignore']

        # Completion should not work without a target.
        for subcommand in subcommands:
            self.assert_no_completions('watchpoint ' + subcommand + ' ')

        # Create a process to provide a target and enable watchpoint setting.
        self.build()
        lldbutil.run_to_source_breakpoint(self, '// Break here', lldb.SBFileSpec("main.cpp"))

        self.runCmd('watchpoint set variable ptr_fooo')
        for subcommand in subcommands:
            self.assert_completions_equal('watchpoint ' + subcommand + ' ', ['1'])

    def test_settings_append_target_er(self):
        self.assert_completions_equal('settings append target.er',
                                      ['target.error-path'])

    def test_settings_insert_after_target_en(self):
        self.assert_completions_equal('settings insert-after target.env', ['target.env-vars'])

    def test_settings_insert_before_target_en(self):
        self.assert_completions_equal(
            'settings insert-before target.env', ['target.env-vars'])

    def test_settings_replace_target_ru(self):
        self.assert_completions_equal(
            'settings replace target.ru', ['target.run-args'])

    def test_settings_show_term(self):
        self.assert_completions_equal(
            'settings show term-', ['term-width'])

    def test_settings_list_term(self):
        self.assert_completions_equal(
            'settings list term-', ['term-width'])

    def test_settings_remove_term(self):
        self.assert_completions_equal('settings remove term-', ['term-width'])

    def test_settings_s(self):
        self.assert_completions_equal('settings s', ['set', 'show'])

    def test_settings_set_th(self):
        self.assert_completions_equal('settings set thread-f', ['thread-format'])

    def test_settings_s_dash(self):
        self.assert_completions_equal('settings set --g', ['--global'])

    def test_settings_clear_th(self):
        self.assert_completions_equal('settings clear thread-f', ['thread-format'])

    def test_settings_set_ta(self):
        self.assert_completions_contain('settings set target.ma', ['target.max-children-count'])

    def test_settings_set_target_exec(self):
        self.assert_completions_equal('settings set target.exec', ['target.exec-search-paths'])

    def test_settings_set_target_pr(self):
        self.assert_completions_contain('settings set target.pr',
                                        ['target.prefer-dynamic-value',
                                        'target.process.thread.step-avoid-regexp'])

    def test_settings_set_target_process(self):
        self.assert_completions_contain('settings set target.process',
                                        ['target.process.disable-memory-cache'])

    def test_settings_set_target_process_dot(self):
        self.assert_completions_contain('settings set target.process.t',
                                        ['target.process.thread.step-out-avoid-nodebug'])

    def test_settings_set_target_process_thread_dot(self):
        self.assert_completions_contain('settings set target.process.thread.',
                                        ['target.process.thread.step-avoid-regexp',
                                        'target.process.thread.trace-thread'])

    def test_thread_plan_discard(self):
        self.build()
        (_, _, thread, _) = lldbutil.run_to_source_breakpoint(self,
                                          'ptr_foo', lldb.SBFileSpec("main.cpp"))
        self.assertTrue(thread)
        self.assert_no_completions('thread plan discard ')

        source_path = os.path.join(self.getSourceDir(), "thread_plan_script.py")
        self.runCmd("command script import '%s'"%(source_path))
        self.runCmd("thread step-scripted -C thread_plan_script.PushPlanStack")
        self.assert_completions_equal('thread plan discard ', ['1'])
        self.runCmd('thread plan discard 1')

    def test_target_space(self):
        self.assert_completions_contain('target ',
                                        ['create',
                                        'delete',
                                        'list',
                                        'modules',
                                        'select',
                                        'stop-hook',
                                        'variable'])

    def test_target_modules_dump_line_table(self):
        """Tests source file completion by completing the line-table argument."""
        self.build()
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.assert_completions_equal('target modules dump line-table main.cp',
                                      ['main.cpp'])

    def test_target_modules_load_aout(self):
        """Tests modules completion by completing the target modules load argument."""
        self.build()
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.assert_completions_equal('target modules load a.ou',
                                     ['a.out'])

    def test_target_modules_search_paths_insert(self):
        # Completion won't work without a valid target.
        self.assert_no_completions("target modules search-paths insert ")
        self.build()
        target = self.dbg.CreateTarget(self.getBuildArtifact('a.out'))
        self.assertTrue(target, VALID_TARGET)
        self.assert_no_completions("target modules search-paths insert ")
        self.runCmd("target modules search-paths add a b")
        self.assert_completions_equal("target modules search-paths insert ", ["0"])
        # Completion only works for the first arg.
        self.assert_no_completions("target modules search-paths insert 0 ")

    def test_target_create_dash_co(self):
        """Test that 'target create --co' completes to 'target variable --core '."""
        self.assert_completions_contain('target create --co', ['--core'])

    def test_target_va(self):
        """Test that 'target va' completes to 'target variable '."""
        self.assert_completions_equal('target va', ['variable'])

    def test_common_completion_thread_index(self):
        subcommands = ['continue', 'info', 'exception', 'select',
                       'step-in', 'step-inst', 'step-inst-over', 'step-out', 'step-over', 'step-script']

        # Completion should do nothing without threads.
        for subcommand in subcommands:
            self.assert_no_completions('thread ' + subcommand + ' ')

        self.build()
        lldbutil.run_to_source_breakpoint(self, '// Break here', lldb.SBFileSpec("main.cpp"))

        # At least we have the thread at the index of 1 now.
        for subcommand in subcommands:
            self.assert_completions_equal('thread ' + subcommand + ' ', ['1'])

    def test_common_completion_type_category_name(self):
        subcommands = ['delete', 'list', 'enable', 'disable', 'define']
        for subcommand in subcommands:
            self.assert_completions_contain('type category ' + subcommand + ' ', ['default'])
        self.assert_completions_contain('type filter add -w ', ['default'])

    def test_command_argument_completion(self):
        """Test completion of command arguments"""
        self.assert_completions_contain("watchpoint set variable -", ['-w', '-s'])
        self.assert_completions_equal('watchpoint set variable -w', ['-w'])
        self.assert_completions_equal("watchpoint set variable --", ['--watch', '--size'])
        self.assert_completions_equal("watchpoint set variable --w", ['--watch'])
        self.assert_completions_equal('watchpoint set variable -w ', ['read', 'write', 'read_write'])
        self.assert_completions_equal("watchpoint set variable --watch ", ['read', 'write', 'read_write'])
        self.assert_completions_equal("watchpoint set variable --watch w", ['write'])
        self.assert_completions_equal('watchpoint set variable -w read_', ['read_write'])
        # Now try the same thing with a variable name (non-option argument) to
        # test that getopts arg reshuffling doesn't confuse us.
        self.assert_completions_contain("watchpoint set variable foo -", ['-w', '-s'])
        self.assert_completions_equal('watchpoint set variable foo -w', ['-w'])
        self.assert_completions_equal("watchpoint set variable foo --", ['--watch', '--size'])
        self.assert_completions_equal("watchpoint set variable foo --w", ['--watch'])
        self.assert_completions_equal('watchpoint set variable foo -w ', ['read', 'write', 'read_write'])
        self.assert_completions_equal("watchpoint set variable foo --watch ", ['read', 'write', 'read_write'])
        self.assert_completions_equal("watchpoint set variable foo --watch w", ['write'])
        self.assert_completions_equal('watchpoint set variable foo -w read_', ['read_write'])

    def test_command_script_delete(self):
        self.runCmd("command script add -h test_desc -f none -s current usercmd1")
        self.check_completion_with_desc('command script delete ', [['usercmd1', 'test_desc']])

    def test_command_delete(self):
        self.runCmd(r"command regex test_command s/^$/finish/ 's/([0-9]+)/frame select %1/'")
        self.assert_completions_equal('command delete test_c', ['test_command'])

    def test_command_unalias(self):
        self.assert_completions_equal('command unalias ima', ['image'])

    def test_completion_description_commands(self):
        """Test descriptions of top-level command completions"""
        self.check_completion_with_desc("", [
            ["command", "Commands for managing custom LLDB commands."],
            ["breakpoint", "Commands for operating on breakpoints (see 'help b' for shorthand.)"]
        ])

        self.check_completion_with_desc("pl", [
            ["platform", "Commands to manage and create platforms."],
            ["plugin", "Commands for managing LLDB plugins."]
        ])

        # Just check that this doesn't crash.
        self.check_completion_with_desc("comman", [])
        self.check_completion_with_desc("non-existent-command", [])

    def test_completion_description_command_options(self):
        """Test descriptions of command options"""
        # Short options
        self.check_completion_with_desc("breakpoint set -", [
            ["-h", "Set the breakpoint on exception catcH."],
            ["-w", "Set the breakpoint on exception throW."]
        ])

        # Long options.
        self.check_completion_with_desc("breakpoint set --", [
            ["--on-catch", "Set the breakpoint on exception catcH."],
            ["--on-throw", "Set the breakpoint on exception throW."]
        ])

        # Ambiguous long options.
        self.check_completion_with_desc("breakpoint set --on-", [
            ["--on-catch", "Set the breakpoint on exception catcH."],
            ["--on-throw", "Set the breakpoint on exception throW."]
        ])

        # Unknown long option.
        self.check_completion_with_desc("breakpoint set --Z", [
        ])

    def test_common_completion_frame_index(self):
        self.build()
        lldbutil.run_to_source_breakpoint(self, '// Break here', lldb.SBFileSpec("main.cpp"))

        self.assert_completions_contain('frame select ', ['0'])
        self.assert_completions_contain('thread backtrace -s ', ['0'])

    def test_frame_recognizer_delete(self):
        self.runCmd("frame recognizer add -l py_class -s module_name -n recognizer_name")
        self.check_completion_with_desc('frame recognizer delete ', [['0', 'py_class, module module_name, symbol recognizer_name']])

    def test_platform_install_local_file(self):
        self.assert_completions_equal('platform target-install main.cp', ['main.cpp'])

    @expectedFailureAll(oslist=["windows"], bugnumber="llvm.org/pr24489")
    def test_symbol_name(self):
        self.build()
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.assert_completions_equal('breakpoint set -n Fo',
                              ['Foo::Bar(int, int)'])
        # No completion for Qu because the candidate is
        # (anonymous namespace)::Quux().
        self.assert_no_completions('breakpoint set -n Qu')

    def test_completion_type_formatter_delete(self):
        self.runCmd('type filter add --child a Aoo')
        self.assert_completions_contain('type filter delete ', ['Aoo'])
        self.runCmd('type filter add --child b -x Boo')
        self.assert_completions_contain('type filter delete ', ['Boo'])

        self.runCmd('type format add -f hex Coo')
        self.assert_completions_contain('type format delete ', ['Coo'])
        self.runCmd('type format add -f hex -x Doo')
        self.assert_completions_contain('type format delete ', ['Doo'])

        self.runCmd('type summary add -c Eoo')
        self.assert_completions_contain('type summary delete ', ['Eoo'])
        self.runCmd('type summary add -x -c Foo')
        self.assert_completions_contain('type summary delete ', ['Foo'])

        self.runCmd('type synthetic add Goo -l test')
        self.assert_completions_contain('type synthetic delete ', ['Goo'])
        self.runCmd('type synthetic add -x Hoo -l test')
        self.assert_completions_contain('type synthetic delete ', ['Hoo'])

    @skipIf(archs=no_match(['x86_64']))
    def test_register_read_and_write_on_x86(self):
        """Test the completion of the commands register read and write on x86"""

        # The tab completion for "register read/write"  won't work without a running process.
        self.assert_no_completions('register read ')
        self.assert_no_completions('register write ')

        self.build()
        self.main_source_spec = lldb.SBFileSpec("main.cpp")
        lldbutil.run_to_source_breakpoint(self, '// Break here', self.main_source_spec)

        # test cases for register read
        self.assert_completions_contain('register read ',
                              ['rax',
                               'rbx',
                               'rcx'])
        self.assert_completions_contain('register read r',
                              ['rax',
                               'rbx',
                               'rcx'])
        # register read can take multiple register names as arguments
        self.assert_completions_contain('register read rax ',
                              ['rax',
                               'rbx',
                               'rcx'])
        # complete with prefix '$'
        self.assert_completions_contain('register read $rb',
                              ['$rbx',
                               '$rbp'])
        self.assert_completions_contain('register read $ra',
                              ['$rax'])
        self.assert_completions_contain('register read rax $',
                              ['$rax',
                               '$rbx',
                               '$rcx'])
        self.assert_completions_contain('register read $rax ',
                              ['rax',
                               'rbx',
                               'rcx'])

        # test cases for register write
        self.assert_completions_contain('register write ',
                              ['rax',
                               'rbx',
                               'rcx'])
        self.assert_completions_contain('register write r',
                              ['rax',
                               'rbx',
                               'rcx'])
        self.assert_completions_contain('register write ra',
                              ['rax'])
        self.assert_completions_contain('register write rb',
                              ['rbx',
                               'rbp'])
        # register write can only take exact one register name as argument
        self.assert_no_completions('register write rbx ')

    def test_common_completion_target_stophook_ids(self):
        subcommands = ['delete', 'enable', 'disable']

        for subcommand in subcommands:
            self.assert_no_completions('target stop-hook ' + subcommand + ' ')

        self.build()
        self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.runCmd('target stop-hook add test DONE')
        self.runCmd('target stop-hook add test DONE')

        for subcommand in subcommands:
            self.assert_completions_equal('target stop-hook ' + subcommand + ' ', ['1', '2'])
            # FIXME: Completing multiple IDs should work too.

    def test_common_completion_type_language(self):
        self.assert_completions_contain('type category define -l ', ['c'])

    def test_target_modules_load_dash_u(self):
        self.build()
        target = self.dbg.CreateTarget(self.getBuildArtifact("a.out"))
        self.assert_completions_contain('target modules load -u ',
                                        [target.GetModuleAtIndex(0).GetUUIDString()])

    def test_complete_breakpoint_with_ids(self):
        """These breakpoint subcommands should be completed with a list of breakpoint ids"""

        subcommands = ['enable', 'disable', 'delete', 'modify', 'name add', 'name delete', 'write']

        # The tab completion here is unavailable without a target
        for subcommand in subcommands:
            self.assert_no_completions('breakpoint ' + subcommand + ' ')

        self.build()
        target = self.dbg.CreateTarget(self.getBuildArtifact('a.out'))
        self.assertTrue(target, VALID_TARGET)

        bp = target.BreakpointCreateByName('main', 'a.out')
        self.assertTrue(bp)
        self.assertEqual(bp.GetNumLocations(), 1)

        for subcommand in subcommands:
            self.assert_completions_contain('breakpoint ' + subcommand + ' ',
                                            ['1'])

        bp2 = target.BreakpointCreateByName('Bar', 'a.out')
        self.assertTrue(bp2)
        self.assertEqual(bp2.GetNumLocations(), 1)

        for subcommand in subcommands:
            self.assert_completions_equal('breakpoint ' + subcommand + ' ',
                                  ['1',
                                   '2'])

        for subcommand in subcommands:
            self.assert_completions_equal('breakpoint ' + subcommand + ' 1 ',
                                  ['1',
                                   '2'])

    def test_complete_breakpoint_with_names(self):
        self.build()
        target = self.dbg.CreateTarget(self.getBuildArtifact('a.out'))
        self.assertTrue(target, VALID_TARGET)

        # test breakpoint read dedicated
        self.assert_no_completions('breakpoint read -N ')
        self.assert_completions_contain('breakpoint read -f breakpoints.json -N ', ['mm'])
        self.assert_no_completions('breakpoint read -f breakpoints.json -N n')
        self.assert_no_completions('breakpoint read -f breakpoints_invalid.json -N ')

        # test common breapoint name completion
        bp1 = target.BreakpointCreateByName('main', 'a.out')
        self.assertTrue(bp1)
        self.assertEqual(bp1.GetNumLocations(), 1)
        self.assert_no_completions('breakpoint set -N n')
        self.assertTrue(bp1.AddNameWithErrorHandling("nn"))
        self.assert_completions_equal('breakpoint set -N ', ["nn"])
