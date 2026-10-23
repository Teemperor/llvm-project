"""The LLDB text user interface.

The 'tui' command (implemented in C++, see CommandObjectTUI.cpp) calls
tui_main() to run the interface and tui_command() for its configuration
subcommands. tui_main() runs in an IOHandler that writes all asynchronous
debugger output into a pipe whose read end is passed as argument.
"""

import errno
import locale
import os
import re
import sys
import time
import traceback

import lldb

from lldb.tui import config as tui_config
from lldb.tui import dialogs, panes, widgets
from lldb.tui.widgets import Surface, curses

_PANE_ORDER = ("source", "variables", "backtrace", "threads", "breakpoints",
               "watchpoints", "console")


def _get_setting(debugger, name):
    """Return the value of an LLDB setting as string (or None)."""
    data = debugger.GetSetting(name)
    if not data or not data.IsValid():
        return None
    kind = data.GetType()
    if kind == lldb.eStructuredDataTypeBoolean:
        return "true" if data.GetBooleanValue() else "false"
    if kind == lldb.eStructuredDataTypeInteger:
        return str(data.GetUnsignedIntegerValue())
    if kind == lldb.eStructuredDataTypeString:
        return data.GetStringValue(4096)
    return None


class ProcessIO(object):
    """A pseudo terminal that is used as stdin/stdout/stderr of processes that
    are launched while the interface is shown. It outlives the interface if a
    process still uses it, in which case its output is forwarded to the
    terminal until the interface is shown again."""

    instances = {}

    def __init__(self):
        import pty
        import threading

        self.master, self.slave = pty.openpty()
        self.path = os.ttyname(self.slave)
        os.set_blocking(self.master, False)
        self.lock = threading.Lock()
        self.forwarding = False
        self.thread = None
        self.closed = False

    @classmethod
    def get(cls, debugger):
        instance = cls.instances.get(debugger.GetID())
        if instance is None or instance.closed:
            instance = cls()
            cls.instances[debugger.GetID()] = instance
        instance.stop_forwarding()
        return instance

    def read(self):
        try:
            data = os.read(self.master, 65536)
        except OSError as e:
            if e.errno in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EINTR):
                return b""
            if e.errno == errno.EIO:
                return b""
            raise
        return data

    def write(self, data):
        try:
            os.write(self.master, data)
        except OSError:
            pass

    def start_forwarding(self, keep_alive):
        """Called when the interface exits."""
        if not keep_alive:
            self.close()
            return
        import select
        import threading

        self.forwarding = True

        def forward():
            while self.forwarding and not self.closed:
                try:
                    ready, _, _ = select.select([self.master], [], [], 0.2)
                except (OSError, ValueError):
                    break
                if not ready or not self.forwarding:
                    continue
                with self.lock:
                    data = self.read()
                if data:
                    os.write(1, data)

        self.thread = threading.Thread(target=forward, name="lldb.tui process output")
        self.thread.daemon = True
        self.thread.start()

    def stop_forwarding(self):
        if self.thread is not None:
            self.forwarding = False
            self.thread.join()
            self.thread = None

    def close(self):
        if not self.closed:
            self.closed = True
            for fd in (self.master, self.slave):
                try:
                    os.close(fd)
                except OSError:
                    pass


class App(object):
    def __init__(self, debugger, async_fd):
        self.debugger = debugger
        self.config = tui_config.get_config(debugger)
        self.colors = widgets.Colors(self.config)
        self.async_fd = async_fd
        self.async_decoder = None
        self.process_decoder = None
        self.process_io = None
        self.expressions = []
        self.prompt_history = []
        self.modals = []
        self.done = False
        self.quit_lldb = False
        self.dirty = True
        self.needs_refresh = True
        self.zoomed = False
        self.screen = None
        self.saved_settings = []
        self.state_key = None
        self.stop_id = None
        self.showing_stop = False
        self.reported_state = None
        self.running_since = None
        self.status = ""
        self.last_poll = 0
        self.last_size = None
        self.config_generation = None
        self.last_click = (0, None)

        self.source = panes.SourcePane(self)
        self.console = panes.ConsolePane(self)
        self.panes = {
            "source": self.source,
            "variables": panes.VariablesPane(self),
            "threads": panes.ThreadsPane(self),
            "backtrace": panes.BacktracePane(self),
            "breakpoints": panes.BreakpointsPane(self),
            "watchpoints": panes.WatchpointsPane(self),
            "console": self.console,
        }
        self.visible = []
        self.focused = "source"
        self.focus_history = []
        self.sidebar_tab = None
        self.tab_regions = []

    # ------------------------------------------------------------------
    # Debugger state
    # ------------------------------------------------------------------

    @property
    def target(self):
        return self.debugger.GetSelectedTarget()

    @property
    def process(self):
        target = self.target
        return target.GetProcess() if target.IsValid() else lldb.SBProcess()

    def process_state(self):
        process = self.process
        return process.GetState() if process.IsValid() else lldb.eStateInvalid

    def is_stopped(self):
        return self.process_state() in (lldb.eStateStopped, lldb.eStateSuspended)

    def is_running(self):
        return self.process_state() in (lldb.eStateRunning, lldb.eStateStepping,
                                        lldb.eStateLaunching, lldb.eStateAttaching)

    def thread(self):
        if not self.is_stopped():
            return None
        thread = self.process.GetSelectedThread()
        return thread if thread.IsValid() else None

    def frame(self):
        thread = self.thread()
        if thread is None:
            return None
        frame = thread.GetSelectedFrame()
        return frame if frame.IsValid() else None

    def frame_compile_unit(self):
        frame = self.frame()
        if frame is None:
            return None
        compile_unit = frame.GetCompileUnit()
        return compile_unit if compile_unit.IsValid() else None

    def require_target(self):
        if self.target.IsValid():
            return True
        self.message("No target. Create one with 'file <path>' in the console.",
                     "console-error")
        return False

    def idle_message(self):
        if not self.target.IsValid():
            return ("No target.\n\nPress ':' and create one with 'file <path>' or\n"
                    "attach to a process with 'process attach -p <pid>'.\n\n"
                    "Press F1 for help and F2 for the settings.")
        if not self.process.IsValid() or self.process_state() in (
                lldb.eStateInvalid, lldb.eStateExited, lldb.eStateDetached,
                lldb.eStateUnloaded, lldb.eStateConnected):
            continue_keys = ", ".join(self.config.keys("continue"))
            return ("Target: %s\n\nThe process is not running. Press %s to launch it\n"
                    "or set breakpoints first (press 'o' to open a source file).\n"
                    % (self.target.GetExecutable().fullpath or "?", continue_keys or ":run"))
        if self.is_running():
            return "The process is running."
        return ""

    def compute_state_key(self):
        process = self.process
        target = self.target
        key = [target.GetExecutable().fullpath if target.IsValid() else None,
               target.GetNumBreakpoints() if target.IsValid() else 0,
               target.GetNumWatchpoints() if target.IsValid() else 0]
        if process.IsValid():
            state = process.GetState()
            key += [process.GetProcessID(), state, process.GetStopID()]
            if state in (lldb.eStateStopped, lldb.eStateSuspended):
                thread = process.GetSelectedThread()
                key += [thread.GetIndexID(), thread.GetSelectedFrame().GetFrameID()]
        return tuple(key)

    def invalidate(self):
        self.needs_refresh = True
        self.dirty = True

    def poll_debugger(self):
        key = self.compute_state_key()
        if key == self.state_key and not self.needs_refresh:
            return
        if self.is_running() and self.showing_stop:
            # Keep showing the last stop for a moment after the process was
            # resumed. Stepping usually stops again right away and clearing
            # the panels in between makes the interface flicker.
            now = time.time()
            if self.running_since is None:
                self.running_since = now
            if (now - self.running_since) * 1000 < self.config.get("running-delay-ms"):
                return
        self.running_since = None
        self.state_key = key
        self.needs_refresh = False
        process = self.process
        self.stop_id = process.GetStopID() if process.IsValid() else None
        for pane in self.panes.values():
            try:
                pane.refresh()
            except Exception:
                self.message("Error while updating the %s panel:\n%s" % (
                    pane.name, traceback.format_exc()), "console-error")
        self.showing_stop = self.is_stopped()
        self.status = self.status_text()
        self.dirty = True

    # ------------------------------------------------------------------
    # Commands and output
    # ------------------------------------------------------------------

    def prompt_text(self):
        return self.debugger.GetPrompt() or "(lldb) "

    def message(self, text, element="async-output"):
        self.console.append_text(text if text.endswith("\n") else text + "\n", element)

    _INTERACTIVE = [
        (re.compile(r"^(gui|tui)\s*$"), None),
        (re.compile(r"^(script|scripting run)(\s+(-l|--language)\s*\S+)?\s*(--)?\s*$"),
         "use 'script <code>' to run Python code"),
        (re.compile(r"^(expression|dwim-print)\b.*(\s--|^\S+)\s*$"),
         "multi-line expressions are not supported, pass the expression as argument"),
        (re.compile(r"^(breakpoint|watchpoint) command add\b(?!.*\s(-o|--one-liner|-F|"
                    r"--python-function|-s|--script-type))"),
         "use 'breakpoint command add -o <command>'"),
        (re.compile(r"^command script add\b(?!.*\s(-f|--function|-c|--class|-p|--parsed))"),
         "use 'command script add -f <function> <name>'"),
    ]

    def check_command(self, command):
        """Returns an error message if the command can't be run inside the
        interface (because it reads from the terminal)."""
        interpreter = self.debugger.GetCommandInterpreter()
        result = lldb.SBCommandReturnObject()
        interpreter.ResolveCommand(command, result)
        resolved = (result.GetOutput() or "").strip() if result.Succeeded() else command.strip()
        if re.match(r"^quit\b", resolved):
            return "quit"
        for pattern, hint in self._INTERACTIVE:
            if pattern.search(resolved):
                message = "'%s' is interactive and can't be used in the TUI" % command.strip()
                return message + ((": " + hint) if hint else ".")
        return None

    def run_command(self, command, echo=True):
        if echo:
            self.console.append_command(self.prompt_text(), command)
        problem = self.check_command(command)
        if problem == "quit":
            self.quit_lldb = True
            self.done = True
            return
        if problem:
            self.message(problem, "console-error")
            return
        result = lldb.SBCommandReturnObject()
        self.debugger.GetCommandInterpreter().HandleCommand(command, result, True)
        self.console.append_text(result.GetOutput(), "text")
        self.console.append_text(result.GetError(), "console-error")
        if result.GetStatus() == lldb.eReturnStatusQuit:
            self.quit_lldb = True
            self.done = True
        self.invalidate()

    def run_action_command(self, command):
        self.run_command(command, echo=self.config.get("echo-actions"))

    def send_process_input(self, text):
        if self.process_io is not None and not self.process_io.closed:
            self.process_io.write(text.encode("utf-8"))
        elif self.process.IsValid():
            self.process.PutSTDIN(text)
        else:
            self.message("There is no process to send input to", "console-error")

    def drain_output(self):
        """Read the asynchronous debugger output and the process output."""
        import codecs

        if self.async_fd is not None:
            if self.async_decoder is None:
                self.async_decoder = codecs.getincrementaldecoder("utf-8")("replace")
            while True:
                try:
                    data = os.read(self.async_fd, 65536)
                except OSError as e:
                    if e.errno not in (errno.EAGAIN, errno.EWOULDBLOCK, errno.EINTR):
                        self.async_fd = None
                    break
                if not data:
                    break
                self.console.append_text(self.async_decoder.decode(data), "async-output",
                                         stream="async")
                self.needs_refresh = True
        # Processes that were launched before the interface was shown write
        # into LLDB's own pseudo terminal. While the interface is shown the
        # debugger doesn't print that output, so it has to be read here.
        process = self.process
        if process.IsValid():
            for read in (process.GetSTDOUT, process.GetSTDERR):
                while True:
                    data = read(65536)
                    if not data:
                        break
                    self.console.append_text(data, "process-output", stream="process")
        if self.process_io is not None and not self.process_io.closed:
            if self.process_decoder is None:
                self.process_decoder = codecs.getincrementaldecoder("utf-8")("replace")
            while True:
                data = self.process_io.read()
                if not data:
                    break
                self.console.append_text(self.process_decoder.decode(data),
                                         "process-output", stream="process")

    def report_process_state(self):
        """Print a short message to the console when the process stopped or
        exited. While the interface is shown the debugger doesn't print its
        usual stop notifications."""
        process = self.process
        if not process.IsValid():
            self.reported_state = None
            return
        state = process.GetState()
        key = (process.GetProcessID(), state, process.GetStopID())
        if key == self.reported_state:
            return
        previous = self.reported_state
        self.reported_state = key
        if previous is None or previous[:2] == key[:2] and state != lldb.eStateStopped:
            return
        pid = process.GetProcessID()
        if state == lldb.eStateExited:
            description = process.GetExitDescription()
            self.message("Process %d exited with status = %d%s" % (
                pid, process.GetExitStatus(),
                " (%s)" % description if description else ""))
        elif state == lldb.eStateCrashed:
            self.message("Process %d crashed" % pid, "console-error")
        elif state == lldb.eStateDetached:
            self.message("Process %d detached" % pid)
        elif state == lldb.eStateStopped:
            thread = process.GetSelectedThread()
            reason = thread.GetStopReason() if thread.IsValid() else lldb.eStopReasonNone
            # Stepping stops are visible in the panels and would only add noise.
            if reason in (lldb.eStopReasonNone, lldb.eStopReasonInvalid,
                          lldb.eStopReasonPlanComplete, lldb.eStopReasonTrace):
                return
            frame = thread.GetSelectedFrame()
            where = frame.GetDisplayFunctionName() or "0x%x" % frame.GetPC()
            location = panes.describe_line_entry(frame.GetLineEntry())
            if location:
                where += " at " + location
            self.message("Process %d stopped: thread #%d, %s, in %s" % (
                pid, thread.GetIndexID(), thread.GetStopDescription(256) or "stopped", where))

    # ------------------------------------------------------------------
    # Modal windows
    # ------------------------------------------------------------------

    def prompt(self, title, text, callback, completer=None):
        self.modals.append(dialogs.PromptDialog(self, title, text, callback, completer))
        self.dirty = True

    def confirm(self, title, message, callback):
        self.modals.append(dialogs.ConfirmDialog(self, title, message, callback))
        self.dirty = True

    def pick(self, title, items, callback, allow_custom=False):
        self.modals.append(dialogs.PickerDialog(self, title, items, callback, allow_custom))
        self.dirty = True

    # ------------------------------------------------------------------
    # Focus and layout
    # ------------------------------------------------------------------

    def focus(self, name):
        pane = self.panes.get(name)
        if pane is None:
            return
        if self.config.get("layout.sidebar-mode") == "tabbed" and name in tui_config.SIDEBAR_PANELS:
            self.sidebar_tab = name
        if name not in self.visible:
            self.layout()
        if name not in self.visible:
            self.message("The %s panel is hidden (see the layout.* settings)" % name, "dim")
            return
        if name != self.focused:
            self.focus_history.append(self.focused)
            del self.focus_history[:-10]
        self.focused = name
        self.dirty = True

    def focus_previous(self):
        while self.focus_history:
            name = self.focus_history.pop()
            if name in self.visible and name != "console":
                self.focused = name
                return
        self.focused = "source"

    def cycle_focus(self, delta):
        order = [name for name in _PANE_ORDER if name in self.visible]
        if self.config.get("layout.sidebar-mode") == "tabbed":
            order = [name for name in _PANE_ORDER if name in self.visible or (
                name in tui_config.SIDEBAR_PANELS
                and name in dict(self.config.sidebar_panels()))]
        if not order:
            return
        index = order.index(self.focused) if self.focused in order else 0
        self.focus(order[(index + delta) % len(order)])

    def layout(self):
        height, width = self.screen.getmaxyx()
        self.chars = widgets.BORDERS[self.config.get("border-style")]
        screen = Surface(self.screen, 0, 0, height, width)
        self.screen_surface = screen
        self.visible = []
        self.tab_regions = []
        for pane in self.panes.values():
            pane.show_title = True
        main = screen.sub(1, 0, height - 1, width)
        console_height = min(self.config.get("layout.console-height"), max(3, main.height // 2))
        if self.zoomed and self.focused != "console":
            self.panes[self.focused].layout(main)
            self.visible = [self.focused]
            return
        console = main.sub(main.height - console_height, 0, console_height, width)
        self.console.layout(console)
        self.visible.append("console")
        upper = main.sub(0, 0, main.height - console_height, width)
        if self.zoomed:
            self.panes["source"].layout(upper)
            self.visible.append("source")
            return

        panels = self.config.sidebar_panels()
        position = self.config.get("layout.sidebar")
        sidebar_width = width * self.config.get("layout.sidebar-width") // 100
        if position == "hidden" or not panels or width < 40:
            sidebar_width = 0
        sidebar_width = min(sidebar_width, width - 20)
        source_width = width - sidebar_width
        if position == "left":
            sidebar = upper.sub(0, 0, upper.height, sidebar_width)
            source = upper.sub(0, sidebar_width, upper.height, source_width)
        else:
            source = upper.sub(0, 0, upper.height, source_width)
            sidebar = upper.sub(0, source_width, upper.height, sidebar_width)
        self.source.layout(source)
        self.visible.append("source")
        if sidebar_width <= 0 or sidebar.height < 3:
            return

        if self.config.get("layout.sidebar-mode") == "tabbed":
            names = [name for name, _ in panels]
            if self.sidebar_tab not in names:
                self.sidebar_tab = self.focused if self.focused in names else names[0]
            pane = self.panes[self.sidebar_tab]
            pane.layout(sidebar)
            pane.show_title = False
            # Leave room for the tab bar in the first row of the content.
            pane.inner = sidebar.sub(2, 1, sidebar.height - 3, sidebar.width - 2)
            self.visible.append(self.sidebar_tab)
            return

        total = sum(weight for _, weight in panels)
        y = 0
        remaining = sidebar.height
        for index, (name, weight) in enumerate(panels):
            if index == len(panels) - 1:
                panel_height = remaining
            else:
                panel_height = max(3, sidebar.height * weight // total)
                panel_height = min(panel_height, remaining - 3 * (len(panels) - index - 1))
            if panel_height < 3:
                continue
            self.panes[name].layout(sidebar.sub(y, 0, panel_height, sidebar.width))
            self.visible.append(name)
            y += panel_height
            remaining -= panel_height

    # ------------------------------------------------------------------
    # Drawing
    # ------------------------------------------------------------------

    def status_text(self):
        target = self.target
        parts = []
        if target.IsValid():
            parts.append(os.path.basename(target.GetExecutable().fullpath or "?"))
            process = self.process
            if process.IsValid():
                state = lldb.SBDebugger.StateAsCString(process.GetState())
                parts.append("pid %d: %s" % (process.GetProcessID(), state))
                thread = self.thread()
                if thread is not None and thread.GetStopReason() not in (
                        lldb.eStopReasonNone, lldb.eStopReasonInvalid):
                    parts.append(thread.GetStopDescription(256) or "")
        else:
            parts.append("no target")
        return "  |  ".join(p for p in parts if p)

    def draw_status_bar(self, screen):
        attr = self.colors.get("status-bar")
        key_attr = self.colors.get("status-key")
        screen.fill(0, attr)
        x = screen.put(0, 0, " LLDB ", key_attr)
        screen.put(0, x, " " + self.status, attr)
        hints = []
        for action, label in (("help", "help"), ("settings", "settings"),
                              ("continue", "cont"), ("step-over", "next"),
                              ("step-into", "step"), ("step-out", "finish"),
                              ("quit", "quit")):
            keys = self.config.keys(action)
            if keys:
                hints.append((tui_config.format_key(keys[0]), label))
        segments = []
        for key, label in hints:
            segments.append((" " + key, key_attr))
            segments.append((" " + label, attr))
        segments.append((" ", attr))
        hint_width = sum(widgets.text_width(t) for t, _ in segments)
        status_width = widgets.text_width(self.status) + 8
        if screen.width - hint_width > status_width:
            screen.put_segments(0, screen.width - hint_width, segments)

    def draw_tabs(self):
        panels = [name for name, _ in self.config.sidebar_panels()]
        if not self.sidebar_tab or self.sidebar_tab not in self.visible:
            return
        pane = self.panes[self.sidebar_tab]
        surface = pane.surface
        x = 1
        self.tab_regions = []
        for name in panels:
            if x >= surface.width - 2:
                break
            label = " %s " % self.panes[name].title_text()
            active = name == self.sidebar_tab
            attr = self.colors.get("tab-active" if active else "tab-inactive")
            width = surface.put(1, x, label, attr, surface.width - 1 - x)
            self.tab_regions.append((surface.y + 1, surface.x + x, width, name))
            x += width + 1

    def draw(self):
        if self.config_generation != self.config.generation:
            self.config_generation = self.config.generation
            self.colors.reset()
            self.screen.timeout(self.config.get("refresh-ms"))
            self.apply_mouse_setting()
            self.invalidate()
            self.poll_debugger()
        self.layout()
        self.screen.erase()
        screen = self.screen_surface
        self.draw_status_bar(screen)
        for name in self.visible:
            pane = self.panes[name]
            try:
                pane.draw(name == self.focused)
            except Exception:
                pane.surface.put(1, 1, "Error: %s" % sys.exc_info()[1], self.colors.get("error"))
                self.message(traceback.format_exc(), "console-error")
        if self.config.get("layout.sidebar-mode") == "tabbed":
            self.draw_tabs()
        cursor = None
        for modal in self.modals:
            modal.draw(screen)
            cursor = modal.cursor
        if not self.modals and self.focused in self.visible:
            cursor = self.panes[self.focused].cursor_position()
        if cursor:
            try:
                curses.curs_set(1)
                self.screen.move(cursor[0], cursor[1])
            except curses.error:
                pass
        else:
            try:
                curses.curs_set(0)
            except curses.error:
                pass
        self.screen.refresh()
        self.dirty = False

    def apply_mouse_setting(self):
        if self.config.get("mouse"):
            mask = curses.ALL_MOUSE_EVENTS
        else:
            mask = 0
        try:
            curses.mousemask(mask)
            curses.mouseinterval(0)
        except curses.error:
            pass

    # ------------------------------------------------------------------
    # Input
    # ------------------------------------------------------------------

    def handle_global_action(self, action):
        if action == "quit":
            self.done = True
        elif action == "help":
            self.modals.append(dialogs.HelpDialog(self))
        elif action == "settings":
            self.modals.append(dialogs.SettingsDialog(self))
        elif action == "redraw":
            self.screen.clear()
            self.invalidate()
        elif action == "focus-next":
            self.cycle_focus(1)
        elif action == "focus-prev":
            self.cycle_focus(-1)
        elif action.startswith("focus-"):
            self.focus(action[len("focus-"):])
        elif action == "zoom":
            self.zoomed = not self.zoomed
        elif action == "continue":
            if self.is_stopped():
                self.run_action_command("process continue")
            elif self.is_running():
                self.message("The process is already running", "dim")
            elif self.require_target():
                self.run_action_command("process launch")
        elif action == "interrupt":
            if self.is_running():
                self.run_action_command("process interrupt")
            else:
                self.message("The process is not running", "dim")
        elif action == "kill":
            if self.process.IsValid() and self.process_state() not in (
                    lldb.eStateExited, lldb.eStateDetached):
                self.run_action_command("process kill")
        elif action in ("step-over", "step-into", "step-out", "step-instruction",
                        "step-over-instruction", "frame-up", "frame-down"):
            if not self.is_stopped():
                self.message("The process must be stopped to %s" % action.replace("-", " "),
                             "console-error")
                return True
            command = {
                "step-over": "thread step-over",
                "step-into": "thread step-in",
                "step-out": "thread step-out",
                "step-instruction": "thread step-inst",
                "step-over-instruction": "thread step-inst-over",
                "frame-up": "up",
                "frame-down": "down",
            }[action]
            self.run_action_command(command)
        else:
            return False
        return True

    def handle_key(self, key):
        name = widgets.key_name(key)
        if name == "Resize":
            # curses already adapted to the new size (resizeterm() queues
            # this key). Calling resizeterm() again would queue another one.
            self.screen.clear()
            self.dirty = True
            return
        if name == "Mouse":
            self.handle_mouse()
            return
        self.dirty = True
        if self.modals:
            modal = self.modals[-1]
            modal.handle_key(key, name)
            self.modals = [m for m in self.modals if not m.done]
            return
        actions = self.config.key_map().get(name, [])
        pane = self.panes[self.focused] if self.focused in self.visible else None
        if pane is self.console:
            # The command line gets all keys first except for function keys
            # and control keys that aren't used for editing.
            editing = (isinstance(key, str) and len(key) == 1 and ord(key) >= 32) or name in (
                "Enter", "Backspace", "Delete", "Left", "Right", "Home", "End", "Up", "Down",
                "PgUp", "PgDn", "Tab", "Esc", "C-a", "C-e", "C-k", "C-u", "C-w", "C-y", "C-t",
                "C-b", "C-f", "C-d", "C-h", "C-p", "C-n")
            if name == "Tab" and (pane.stdin_mode or not pane.input.text.strip()):
                # Tab only completes when there is something to complete.
                # Otherwise it moves the focus like everywhere else.
                editing = False
            if editing and pane.handle_key(key, name):
                return
            for action in actions:
                if pane.handle_action(action, key, name) or self.handle_global_action(action):
                    return
            return
        for action in actions:
            if pane is not None and pane.handle_action(action, key, name):
                return
            if self.handle_global_action(action):
                return
        if pane is not None and pane.handle_key(key, name):
            return
        if isinstance(key, str) and len(key) == 1 and ord(key) >= 32 and not actions:
            # Typing a character that isn't bound starts a command.
            self.focus("console")
            self.console.handle_key(key, name)

    def handle_mouse(self):
        try:
            _, x, y, _, state = curses.getmouse()
        except curses.error:
            return
        self.dirty = True
        wheel_up = state & getattr(curses, "BUTTON4_PRESSED", 0)
        wheel_down = state & getattr(curses, "BUTTON5_PRESSED", 1 << 21)
        clicked = state & (curses.BUTTON1_PRESSED | curses.BUTTON1_CLICKED
                           | curses.BUTTON1_DOUBLE_CLICKED | curses.BUTTON1_RELEASED)
        if self.modals:
            modal = self.modals[-1]
            if wheel_up or wheel_down:
                modal.scroll(-3 if wheel_up else 3)
            elif state & (curses.BUTTON1_PRESSED | curses.BUTTON1_CLICKED):
                modal.handle_mouse(y - 3, x, 1)
            self.modals = [m for m in self.modals if not m.done]
            return
        for top, left, width, name in self.tab_regions:
            if y == top and left <= x < left + width and clicked:
                self.focus(name)
                return
        for name in self.visible:
            pane = self.panes[name]
            surface = pane.surface
            if not (surface.y <= y < surface.y + surface.height
                    and surface.x <= x < surface.x + surface.width):
                continue
            if wheel_up or wheel_down:
                pane.scroll(-3 if wheel_up else 3)
                return
            if not (state & (curses.BUTTON1_PRESSED | curses.BUTTON1_CLICKED
                             | curses.BUTTON1_DOUBLE_CLICKED)):
                return
            self.focus(name)
            now = time.time()
            clicks = 1
            if state & curses.BUTTON1_DOUBLE_CLICKED:
                clicks = 2
            elif self.last_click[1] == (y, x) and now - self.last_click[0] < 0.4:
                clicks = 2
            self.last_click = (now, (y, x))
            inner = pane.inner
            if inner and inner.y <= y < inner.y + inner.height:
                pane.handle_mouse(y - inner.y, x - inner.x, clicks)
            return

    @staticmethod
    def terminal_size():
        # LLDB redirects Python's sys.stdout, so ask the file descriptors of
        # the terminal directly.
        for fd in (1, 0, 2):
            try:
                size = os.get_terminal_size(fd)
                return size.lines, size.columns
            except OSError:
                continue
        return None

    def handle_resize(self):
        size = self.terminal_size()
        if size:
            try:
                curses.resizeterm(*size)
            except curses.error:
                pass
        self.screen.clear()
        self.dirty = True

    def check_size(self):
        """LLDB's driver handles SIGWINCH, so curses doesn't notice when the
        terminal is resized. Poll the size instead."""
        size = self.terminal_size()
        if size and size != self.screen.getmaxyx():
            self.handle_resize()

    # ------------------------------------------------------------------
    # Setup and main loop
    # ------------------------------------------------------------------

    def override_setting(self, name, value):
        old = _get_setting(self.debugger, name)
        if old is None or old == value:
            return
        self.saved_settings.append((name, old))
        self.debugger.HandleCommand("settings set -- %s %s" % (name, value))

    def setup_debugger(self):
        self.saved_async = self.debugger.GetAsync()
        self.debugger.SetAsync(True)
        # The statusline draws over the curses screen and confirmations would
        # read from the terminal.
        self.override_setting("show-statusline", "false")
        self.override_setting("auto-confirm", "true")
        if self.config.get("capture-process-io") and hasattr(os, "openpty"):
            target = self.target
            used_paths = [_get_setting(self.debugger, "target.%s-path" % n)
                          for n in ("input", "output", "error")]
            if not any(used_paths):
                try:
                    self.process_io = ProcessIO.get(self.debugger)
                except (OSError, ImportError) as e:
                    self.message("Unable to create a pseudo terminal for the process: %s" % e,
                                 "console-error")
                if self.process_io is not None:
                    for name in ("input", "output", "error"):
                        self.override_setting("target.%s-path" % name, self.process_io.path)

    def restore_debugger(self):
        for name, value in reversed(self.saved_settings):
            if value == "":
                self.debugger.HandleCommand("settings clear %s" % name)
            else:
                self.debugger.HandleCommand("settings set -- %s %s" % (name, value))
        self.saved_settings = []
        self.debugger.SetAsync(self.saved_async)
        if self.process_io is not None:
            process = self.process
            alive = process.IsValid() and process.GetState() not in (
                lldb.eStateExited, lldb.eStateDetached, lldb.eStateInvalid)
            self.process_io.start_forwarding(alive)

    def run(self, screen):
        self.screen = screen
        self.colors.init()
        curses.raw()
        curses.noecho()
        screen.keypad(True)
        screen.timeout(self.config.get("refresh-ms"))
        try:
            curses.set_escdelay(25)
        except (AttributeError, curses.error):
            pass
        self.apply_mouse_setting()
        for warning in getattr(self.config, "load_warnings", []):
            self.message("warning: " + warning, "console-error")
        self.config.load_warnings = []
        self.message("Welcome to the LLDB TUI. Press F1 for help, F2 for settings and "
                     "':' to enter commands.", "dim")
        self.layout()
        while not self.done:
            self.drain_output()
            self.report_process_state()
            now = time.time()
            if self.needs_refresh or now - self.last_poll > self.config.get("refresh-ms") / 1000.0:
                self.last_poll = now
                self.poll_debugger()
            self.check_size()
            if self.dirty:
                self.draw()
            try:
                key = screen.get_wch()
            except curses.error:
                continue
            except KeyboardInterrupt:
                key = "\x03"
            try:
                self.handle_key(key)
            except Exception:
                self.message(traceback.format_exc(), "console-error")
            # Handle all pending keys before drawing again.
            screen.nodelay(True)
            try:
                for _ in range(64):
                    try:
                        key = screen.get_wch()
                    except curses.error:
                        break
                    try:
                        self.handle_key(key)
                    except Exception:
                        self.message(traceback.format_exc(), "console-error")
                    if self.done:
                        break
            finally:
                screen.nodelay(False)
                screen.timeout(self.config.get("refresh-ms"))


def _run_curses(app):
    locale.setlocale(locale.LC_ALL, "")
    os.environ.setdefault("ESCDELAY", "25")
    screen = curses.initscr()
    try:
        app.run(screen)
    finally:
        try:
            curses.mousemask(0)
        except curses.error:
            pass
        curses.noraw()
        curses.echo()
        screen.keypad(False)
        curses.endwin()


def tui_main(debugger, command, exe_ctx, result, internal_dict):
    """Run the text user interface. Called by the 'tui' command with the
    file descriptor of the pipe that receives asynchronous output."""
    if curses is None:
        result.SetError("the tui command requires the Python curses module")
        return
    async_fd = int(command) if command.strip() else None
    if async_fd is not None:
        os.set_blocking(async_fd, False)
    app = App(debugger, async_fd)
    try:
        app.setup_debugger()
        _run_curses(app)
    except Exception:
        result.SetError("the text user interface failed:\n" + traceback.format_exc())
    finally:
        app.restore_debugger()
    if app.quit_lldb:
        debugger.HandleCommand("quit")


def tui_command(debugger, command, exe_ctx, result, internal_dict):
    """Handle 'tui <subcommand>'."""
    tui_config.run_subcommand(debugger, command, result)
