"""Configuration of the LLDB text user interface.

Every aspect of the interface (layout, colors, key bindings, behavior of the
individual panels) is described by an option in OPTIONS. Options can be changed
from within the interface (settings window) or with the 'tui set' command, e.g.
from an .lldbinit file:

    tui set theme monokai
    tui set layout.sidebar left
    tui set color.keyword "magenta,default,bold"
    tui bind step-over F10,n

The configuration can be saved to and loaded from a JSON file ('tui save' and
'tui load'). The default file is loaded automatically the first time the
configuration of a debugger is accessed.
"""

import difflib
import json
import os

# ----------------------------------------------------------------------------
# Option schema
# ----------------------------------------------------------------------------


class Option(object):
    def __init__(self, name, kind, default, description, choices=None, minimum=None):
        self.name = name
        # One of: bool, int, str, enum, color, keys, panels.
        self.kind = kind
        self.default = default
        self.description = description
        self.choices = choices
        self.minimum = minimum

    @property
    def category(self):
        if "." in self.name:
            return self.name.split(".", 1)[0]
        return "general"

    def parse(self, text):
        """Convert user input into a value of this option or raise ValueError."""
        text = text.strip()
        if len(text) >= 2 and text[0] == text[-1] and text[0] in "\"'":
            text = text[1:-1]
        if self.kind == "bool":
            lowered = text.lower()
            if lowered in ("1", "true", "yes", "on", "enable", "enabled"):
                return True
            if lowered in ("0", "false", "no", "off", "disable", "disabled"):
                return False
            raise ValueError("'%s' is not a boolean value" % text)
        if self.kind == "int":
            try:
                value = int(text, 0)
            except ValueError:
                raise ValueError("'%s' is not an integer" % text)
            if self.minimum is not None and value < self.minimum:
                raise ValueError("%s must be at least %d" % (self.name, self.minimum))
            return value
        if self.kind == "enum":
            if text not in self.choices:
                raise ValueError(
                    "'%s' is not a valid value for %s (expected one of: %s)"
                    % (text, self.name, ", ".join(self.choices))
                )
            return text
        if self.kind == "color":
            parse_color_spec(text)
            return text
        if self.kind == "keys":
            parse_key_list(text)
            return text
        if self.kind == "panels":
            parse_panel_list(text)
            return text
        return text

    def format(self, value):
        if self.kind == "bool":
            return "true" if value else "false"
        return str(value)


SIDEBAR_PANELS = ("variables", "threads", "backtrace", "breakpoints", "watchpoints")

THEMES = ("default", "monokai", "solarized-dark", "light", "mono")

OPTIONS = [
    # General.
    Option("theme", "enum", "default", "The color theme. Individual colors can be "
           "overridden with the color.* options.", choices=THEMES),
    Option("mouse", "bool", True, "Enable mouse support (click to focus and "
           "select, wheel to scroll). Disable this to use the terminal's own "
           "text selection."),
    Option("border-style", "enum", "unicode", "The characters used to draw "
           "borders and markers.", choices=("unicode", "ascii")),
    Option("refresh-ms", "int", 100, "How often (in milliseconds) the interface "
           "polls the debugger for changes.", minimum=10),
    Option("running-delay-ms", "int", 500, "How long (in milliseconds) the "
           "panels keep showing the last stop after the process was resumed "
           "before they switch to showing that the process is running. This "
           "avoids flickering while stepping.", minimum=0),
    Option("echo-actions", "bool", True, "Print the LLDB command that is run for "
           "an action that is triggered by a key (e.g. stepping) to the console."),
    Option("capture-process-io", "bool", True, "Redirect the input and output of "
           "processes that are launched while the interface is shown to a "
           "pseudo terminal that is displayed in the console."),
    # Layout.
    Option("layout.sidebar", "enum", "right", "Where the sidebar with the "
           "auxiliary panels is shown.", choices=("right", "left", "hidden")),
    Option("layout.sidebar-width", "int", 42, "Width of the sidebar in percent "
           "of the terminal width.", minimum=10),
    Option("layout.sidebar-mode", "enum", "stacked", "Show the sidebar panels "
           "below each other ('stacked') or as tabs ('tabbed').",
           choices=("stacked", "tabbed")),
    Option("layout.sidebar-panels", "panels",
           "variables:3,backtrace:2,threads:1,breakpoints:1,watchpoints:1",
           "Comma separated list of the panels in the sidebar in display order. "
           "Each entry can have a ':<weight>' suffix that controls how much "
           "space the panel gets in stacked mode. Available panels: "
           + ", ".join(SIDEBAR_PANELS) + "."),
    Option("layout.console-height", "int", 10, "Height of the console panel "
           "(including its border and the command line).", minimum=3),
    # Source view.
    Option("source.syntax-highlight", "bool", True, "Syntax highlight source "
           "code."),
    Option("source.line-numbers", "bool", True, "Show line numbers."),
    Option("source.tab-width", "int", 4, "Number of columns a tab character "
           "expands to.", minimum=1),
    Option("source.scroll-margin", "int", 4, "Number of lines that are kept "
           "visible above and below the current line.", minimum=0),
    Option("source.highlight-pc-line", "bool", True, "Highlight the whole line "
           "of the current program counter (not only the marker)."),
    Option("source.disassembly", "enum", "auto", "When to show disassembly "
           "instead of source code: 'auto' shows disassembly when no source is "
           "available.", choices=("auto", "always", "never")),
    Option("source.show-bytes", "bool", False, "Show opcode bytes in the "
           "disassembly view."),
    # Variables.
    Option("variables.show-types", "bool", True, "Show the type of variables."),
    Option("variables.show-arguments", "bool", True, "Show function arguments."),
    Option("variables.show-locals", "bool", True, "Show local variables."),
    Option("variables.show-statics", "bool", False, "Show static variables."),
    Option("variables.show-registers", "bool", True, "Show a 'Registers' group "
           "with the register sets of the selected frame."),
    Option("variables.dynamic-types", "bool", True, "Show the dynamic type of "
           "values (requires running code in the inferior for some languages)."),
    Option("variables.highlight-changes", "bool", True, "Highlight values that "
           "changed since the last stop."),
    Option("variables.max-children", "int", 100, "How many children of a value "
           "are fetched at once when it is expanded.", minimum=1),
    # Backtrace.
    Option("backtrace.show-addresses", "bool", False, "Show the program counter "
           "of each frame."),
    Option("backtrace.show-modules", "bool", True, "Show the module of each "
           "frame."),
    Option("backtrace.show-hidden", "bool", False, "Show frames that LLDB "
           "hides by default (e.g. standard library implementation details)."),
    Option("backtrace.max-frames", "int", 256, "Maximum number of frames that "
           "are shown.", minimum=1),
    # Threads.
    Option("threads.show-queues", "bool", True, "Show the dispatch queue of "
           "threads."),
    Option("threads.show-frame", "bool", True, "Show the function the thread is "
           "stopped in."),
    # Console.
    Option("console.scrollback", "int", 10000, "Number of lines kept in the "
           "console.", minimum=100),
    Option("console.history-size", "int", 1000, "Number of commands kept in the "
           "command line history.", minimum=1),
    Option("console.repeat-on-empty", "bool", True, "Pressing Enter on an empty "
           "command line repeats the previous command."),
]

# Colors. Each color is specified as 'fg[,bg[,attr...]]' where fg/bg are
# 'default', one of the color names below, 'bright-<name>' or a number between
# 0 and 255, and attributes are any of: bold, dim, italic, underline, reverse,
# standout, blink.
COLOR_ELEMENTS = [
    ("text", "Normal text."),
    ("dim", "Less important text."),
    ("border", "Borders of panels."),
    ("border-focus", "Border of the focused panel."),
    ("title", "Titles of panels."),
    ("title-focus", "Title of the focused panel."),
    ("status-bar", "The status bar at the top."),
    ("status-key", "Key hints in the status bar."),
    ("tab-active", "The active tab in tabbed mode."),
    ("tab-inactive", "Inactive tabs in tabbed mode."),
    ("selection", "The selected row of the focused panel."),
    ("selection-unfocused", "The selected row of other panels."),
    ("line-number", "Line numbers."),
    ("pc-line", "The line of the program counter."),
    ("frame-line", "The line of the selected frame when it isn't frame 0."),
    ("pc-marker", "The marker of the program counter."),
    ("breakpoint", "Enabled breakpoint markers."),
    ("breakpoint-disabled", "Disabled breakpoint markers."),
    ("search-match", "Search matches."),
    ("keyword", "Keywords."),
    ("type", "Type names."),
    ("string", "String and character literals."),
    ("number", "Number literals."),
    ("comment", "Comments."),
    ("preprocessor", "Preprocessor directives."),
    ("function", "Function names."),
    ("address", "Addresses."),
    ("var-name", "Variable names."),
    ("var-type", "Variable types."),
    ("var-value", "Variable values."),
    ("var-changed", "Values that changed since the last stop."),
    ("stop-reason", "Stop reasons."),
    ("error", "Errors."),
    ("prompt", "The command line prompt."),
    ("console-command", "Echoed commands in the console."),
    ("console-error", "Error output in the console."),
    ("async-output", "Asynchronous debugger output in the console."),
    ("process-output", "Output of the debugged process."),
    ("dialog", "Dialog windows."),
    ("dialog-title", "Titles of dialog windows."),
    ("input", "Text input fields."),
]

THEME_COLORS = {
    "default": {
        "text": "default",
        "dim": "bright-black",
        "border": "bright-black",
        "border-focus": "cyan,default,bold",
        "title": "default,default,bold",
        "title-focus": "black,cyan,bold",
        "status-bar": "white,blue",
        "status-key": "yellow,blue,bold",
        "tab-active": "black,cyan,bold",
        "tab-inactive": "default",
        "selection": "black,cyan",
        "selection-unfocused": "default,default,reverse",
        "line-number": "bright-black",
        "pc-line": "black,yellow",
        "frame-line": "black,green",
        "pc-marker": "yellow,default,bold",
        "breakpoint": "red,default,bold",
        "breakpoint-disabled": "bright-black",
        "search-match": "black,magenta",
        "keyword": "magenta,default,bold",
        "type": "cyan",
        "string": "green",
        "number": "yellow",
        "comment": "bright-black,default,italic",
        "preprocessor": "blue,default,bold",
        "function": "bright-blue",
        "address": "yellow",
        "var-name": "cyan",
        "var-type": "bright-black",
        "var-value": "default",
        "var-changed": "red,default,bold",
        "stop-reason": "yellow,default,bold",
        "error": "red,default,bold",
        "prompt": "green,default,bold",
        "console-command": "default,default,bold",
        "console-error": "red",
        "async-output": "yellow",
        "process-output": "bright-green",
        "dialog": "default",
        "dialog-title": "black,cyan,bold",
        "input": "default,default,underline",
    },
    "monokai": {
        "border": "241",
        "border-focus": "148,default,bold",
        "title-focus": "235,148,bold",
        "status-bar": "252,237",
        "status-key": "208,237,bold",
        "tab-active": "235,148,bold",
        "selection": "235,81",
        "selection-unfocused": "default,238",
        "line-number": "242",
        "pc-line": "235,186",
        "frame-line": "235,148",
        "pc-marker": "208,default,bold",
        "breakpoint": "197,default,bold",
        "breakpoint-disabled": "242",
        "keyword": "197",
        "type": "81,default,italic",
        "string": "186",
        "number": "141",
        "comment": "242,default,italic",
        "preprocessor": "197",
        "function": "148",
        "address": "141",
        "var-name": "81",
        "var-type": "242",
        "var-changed": "208,default,bold",
        "stop-reason": "208,default,bold",
        "error": "197,default,bold",
        "prompt": "148,default,bold",
        "async-output": "186",
        "process-output": "148",
        "dialog-title": "235,148,bold",
    },
    "solarized-dark": {
        "text": "default",
        "dim": "240",
        "border": "240",
        "border-focus": "37,default,bold",
        "title-focus": "234,37,bold",
        "status-bar": "245,235",
        "status-key": "136,235,bold",
        "tab-active": "234,37,bold",
        "selection": "234,33",
        "selection-unfocused": "default,236",
        "line-number": "240",
        "pc-line": "234,136",
        "frame-line": "234,64",
        "pc-marker": "136,default,bold",
        "breakpoint": "160,default,bold",
        "breakpoint-disabled": "240",
        "keyword": "64",
        "type": "136",
        "string": "37",
        "number": "125",
        "comment": "240,default,italic",
        "preprocessor": "166",
        "function": "33",
        "address": "125",
        "var-name": "33",
        "var-type": "240",
        "var-changed": "166,default,bold",
        "stop-reason": "136,default,bold",
        "error": "160,default,bold",
        "prompt": "64,default,bold",
        "async-output": "136",
        "process-output": "64",
        "dialog-title": "234,37,bold",
    },
    "light": {
        "dim": "245",
        "border": "250",
        "border-focus": "25,default,bold",
        "title-focus": "white,25,bold",
        "status-bar": "black,252",
        "status-key": "25,252,bold",
        "tab-active": "white,25,bold",
        "selection": "white,25",
        "selection-unfocused": "default,254",
        "line-number": "246",
        "pc-line": "black,229",
        "frame-line": "black,194",
        "pc-marker": "166,default,bold",
        "breakpoint": "160,default,bold",
        "breakpoint-disabled": "248",
        "search-match": "black,219",
        "keyword": "25,default,bold",
        "type": "30",
        "string": "28",
        "number": "130",
        "comment": "245,default,italic",
        "preprocessor": "90",
        "function": "94",
        "address": "130",
        "var-name": "25",
        "var-type": "245",
        "var-changed": "160,default,bold",
        "stop-reason": "166,default,bold",
        "error": "160,default,bold",
        "prompt": "28,default,bold",
        "async-output": "130",
        "process-output": "28",
        "dialog-title": "white,25,bold",
    },
    "mono": dict(
        [(name, "default") for name, _ in COLOR_ELEMENTS],
        **{
            "dim": "default,default,dim",
            "border-focus": "default,default,bold",
            "title": "default,default,bold",
            "title-focus": "default,default,reverse,bold",
            "status-bar": "default,default,reverse",
            "status-key": "default,default,reverse,bold",
            "tab-active": "default,default,reverse,bold",
            "selection": "default,default,reverse",
            "selection-unfocused": "default,default,underline",
            "line-number": "default,default,dim",
            "pc-line": "default,default,reverse,bold",
            "frame-line": "default,default,reverse",
            "pc-marker": "default,default,bold",
            "breakpoint": "default,default,bold",
            "breakpoint-disabled": "default,default,dim",
            "search-match": "default,default,reverse,underline",
            "keyword": "default,default,bold",
            "comment": "default,default,dim",
            "var-name": "default,default,bold",
            "var-type": "default,default,dim",
            "var-changed": "default,default,bold,underline",
            "stop-reason": "default,default,bold",
            "error": "default,default,bold",
            "prompt": "default,default,bold",
            "console-command": "default,default,bold",
            "dialog-title": "default,default,reverse,bold",
            "input": "default,default,underline",
        }
    ),
}

for _element, _description in COLOR_ELEMENTS:
    OPTIONS.append(
        Option("color." + _element, "color", "", _description + " Empty means "
               "the value of the current theme.")
    )

# Key bindings. Keys are given as a comma separated list of key names: single
# characters, 'C-x' for control combinations, F1-F24 ('S-F5' is an alias for
# 'F17'), and the names in SPECIAL_KEYS.
ACTIONS = [
    # Global actions.
    ("quit", "q,C-q", "Leave the text user interface."),
    ("help", "F1,?", "Show the help window."),
    ("settings", "F2", "Open the settings window."),
    ("redraw", "C-l", "Redraw the screen."),
    ("focus-next", "Tab", "Focus the next panel."),
    ("focus-prev", "S-Tab", "Focus the previous panel."),
    ("focus-console", ":", "Focus the command line."),
    ("focus-source", "1", "Focus the source panel."),
    ("focus-variables", "2", "Focus the variables panel."),
    ("focus-backtrace", "3", "Focus the backtrace panel."),
    ("focus-threads", "4", "Focus the threads panel."),
    ("focus-breakpoints", "5", "Focus the breakpoints panel."),
    ("focus-watchpoints", "6", "Focus the watchpoints panel."),
    ("zoom", "z", "Toggle showing the focused panel in the whole main area."),
    ("continue", "F5,c", "Continue the process (launch it if there is none)."),
    ("interrupt", "F6,C-c", "Interrupt the running process."),
    ("kill", "K", "Kill the process."),
    ("step-over", "F10,n", "Step over the current source line."),
    ("step-into", "F11,s", "Step into the current source line."),
    ("step-out", "S-F11,F12,f", "Step out of the current function."),
    ("step-instruction", "i", "Step one instruction."),
    ("step-over-instruction", "I", "Step one instruction, stepping over calls."),
    ("frame-up", "<", "Select the calling frame."),
    ("frame-down", ">", "Select the frame called by the selected frame."),
    # Navigation within panels.
    ("up", "Up,k", "Move up."),
    ("down", "Down,j", "Move down."),
    ("page-up", "PgUp,C-b", "Move a page up."),
    ("page-down", "PgDn,C-f", "Move a page down."),
    ("top", "Home,g", "Move to the top."),
    ("bottom", "End,G", "Move to the bottom."),
    ("activate", "Enter", "Select/open the selected item. Source: continue to "
     "the selected position (asks for confirmation)."),
    ("expand", "Right,l", "Expand the selected item. Source: move the cursor "
     "right."),
    ("collapse", "Left,h", "Collapse the selected item. Source: move the cursor "
     "left."),
    ("line-start", "0,^", "Source: move the cursor to the start of the line."),
    ("line-end", "$", "Source: move the cursor to the end of the line."),
    # Panel specific actions.
    ("toggle-breakpoint", "F9,b", "Source: set/remove a breakpoint on the "
     "selected line."),
    ("toggle-breakpoint-enabled", "B", "Source: enable/disable the breakpoints "
     "on the selected line."),
    ("run-to-line", "F4,u", "Source: continue to the selected position "
     "without asking for confirmation."),
    ("goto-pc", "p", "Source: show the location of the selected frame."),
    ("goto-line", "L", "Source: go to a line number."),
    ("open-file", "o", "Source: open a source file of the target."),
    ("toggle-disassembly", "d", "Source: switch between source and "
     "disassembly."),
    ("search", "/", "Source/console: search for text."),
    ("search-next", "F3,N", "Source: go to the next search match."),
    ("toggle", "Space", "Breakpoints/watchpoints: enable or disable the "
     "selected item."),
    ("delete", "Delete,x", "Breakpoints/watchpoints/expressions: delete the "
     "selected item."),
    ("add", "a", "Breakpoints/watchpoints/variables: add a breakpoint, "
     "watchpoint or watched expression."),
    ("edit", "e", "Breakpoints/watchpoints: edit the condition. Variables: "
     "change the value."),
    ("watch", "w", "Variables: set a watchpoint on the selected variable."),
    ("print", "P", "Variables: print the selected value to the console."),
]

for _action, _keys, _description in ACTIONS:
    OPTIONS.append(Option("key." + _action, "keys", _keys, _description))

OPTIONS_BY_NAME = dict((o.name, o) for o in OPTIONS)

CATEGORY_TITLES = [
    ("general", "General"),
    ("layout", "Layout"),
    ("source", "Source"),
    ("variables", "Variables"),
    ("backtrace", "Backtrace"),
    ("threads", "Threads"),
    ("console", "Console"),
    ("color", "Colors"),
    ("key", "Key bindings"),
]

# ----------------------------------------------------------------------------
# Value parsers
# ----------------------------------------------------------------------------

COLOR_NAMES = {
    "black": 0,
    "red": 1,
    "green": 2,
    "yellow": 3,
    "blue": 4,
    "magenta": 5,
    "cyan": 6,
    "white": 7,
}

COLOR_ATTRIBUTES = ("bold", "dim", "italic", "underline", "reverse", "standout", "blink")


def parse_color(text):
    """Parse a single color into -1 (default) or a color number."""
    text = text.strip().lower()
    if text in ("", "default", "none"):
        return -1
    if text in COLOR_NAMES:
        return COLOR_NAMES[text]
    if text.startswith("bright-") and text[7:] in COLOR_NAMES:
        return COLOR_NAMES[text[7:]] + 8
    try:
        value = int(text)
    except ValueError:
        raise ValueError("'%s' is not a color" % text)
    if not 0 <= value <= 255:
        raise ValueError("color numbers must be between 0 and 255")
    return value


def parse_color_spec(text):
    """Parse 'fg[,bg[,attr...]]' into (fg, bg, [attr...])."""
    parts = [p.strip() for p in text.split(",")] if text.strip() else []
    fg, bg, attrs = -1, -1, []
    colors = []
    for part in parts:
        if part.lower() in COLOR_ATTRIBUTES:
            attrs.append(part.lower())
        elif len(colors) < 2:
            colors.append(parse_color(part))
        else:
            raise ValueError("'%s' is neither a color nor an attribute" % part)
    if colors:
        fg = colors[0]
    if len(colors) > 1:
        bg = colors[1]
    return fg, bg, attrs


SPECIAL_KEYS = {
    "tab": "Tab",
    "s-tab": "S-Tab",
    "btab": "S-Tab",
    "enter": "Enter",
    "return": "Enter",
    "esc": "Esc",
    "escape": "Esc",
    "space": " ",
    "spc": " ",
    "up": "Up",
    "down": "Down",
    "left": "Left",
    "right": "Right",
    "pgup": "PgUp",
    "pageup": "PgUp",
    "pgdn": "PgDn",
    "pagedown": "PgDn",
    "home": "Home",
    "end": "End",
    "delete": "Delete",
    "del": "Delete",
    "insert": "Insert",
    "ins": "Insert",
    "backspace": "Backspace",
    "comma": ",",
}


def parse_key(text):
    """Convert a key name from the configuration into the canonical name that
    the input handling produces for that key."""
    if text == "":
        raise ValueError("empty key name")
    if len(text) == 1:
        return text
    lowered = text.lower()
    if lowered in SPECIAL_KEYS:
        return SPECIAL_KEYS[lowered]
    if lowered.startswith("c-") and len(text) == 3:
        char = text[2].lower()
        if "a" <= char <= "z":
            return "C-" + char
        raise ValueError("unsupported control key '%s'" % text)
    shifted = lowered.startswith("s-f")
    number_text = lowered[3:] if shifted else lowered[1:]
    if (lowered.startswith("f") or shifted) and number_text.isdigit():
        number = int(number_text) + (12 if shifted else 0)
        if 1 <= number <= 24 and (not shifted or number > 12):
            return "F%d" % number
    raise ValueError("unknown key '%s'" % text)


def parse_key_list(text):
    """Parse a comma separated list of keys. ',' itself is spelled 'comma'."""
    if not text.strip():
        return []
    return [parse_key(k.strip()) for k in text.split(",") if k.strip()]


def format_key(key):
    if key == " ":
        return "Space"
    if key == ",":
        return "comma"
    if key.startswith("F") and key[1:].isdigit() and int(key[1:]) > 12:
        return "S-F%d" % (int(key[1:]) - 12)
    return key


def parse_panel_list(text):
    """Parse 'name[:weight],...' into a list of (name, weight)."""
    result = []
    for entry in text.split(","):
        entry = entry.strip()
        if not entry:
            continue
        name, _, weight = entry.partition(":")
        name = name.strip()
        if name not in SIDEBAR_PANELS:
            raise ValueError(
                "unknown panel '%s' (available: %s)" % (name, ", ".join(SIDEBAR_PANELS))
            )
        if any(n == name for n, _ in result):
            raise ValueError("panel '%s' is listed twice" % name)
        try:
            weight = int(weight) if weight.strip() else 1
        except ValueError:
            raise ValueError("invalid weight '%s' for panel '%s'" % (weight, name))
        if weight < 1:
            raise ValueError("the weight of panel '%s' must be positive" % name)
        result.append((name, weight))
    return result


# ----------------------------------------------------------------------------
# Configuration store
# ----------------------------------------------------------------------------


def default_config_path():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(
        os.path.expanduser("~"), ".config"
    )
    return os.path.join(base, "lldb", "tui.json")


class Config(object):
    """The configuration of the interface of one debugger."""

    def __init__(self):
        # Only values that differ from the default are stored.
        self.values = {}
        self.listeners = []
        self.generation = 0

    def get(self, name):
        if name in self.values:
            return self.values[name]
        return OPTIONS_BY_NAME[name].default

    def set(self, name, value):
        option = self.lookup(name)
        if not isinstance(value, str):
            value = option.format(value)
        value = option.parse(value)
        if value == option.default:
            self.values.pop(option.name, None)
        else:
            self.values[option.name] = value
        self._changed()

    def reset(self, name=None):
        if name is None:
            self.values.clear()
        else:
            self.values.pop(self.lookup(name).name, None)
        self._changed()

    def _changed(self):
        self.generation += 1
        for listener in list(self.listeners):
            listener()

    @staticmethod
    def lookup(name):
        option = OPTIONS_BY_NAME.get(name)
        if option:
            return option
        # Allow 'tui set keyword magenta' and 'tui set step-over F10'.
        for prefix in ("color.", "key."):
            if prefix + name in OPTIONS_BY_NAME:
                return OPTIONS_BY_NAME[prefix + name]
        message = "unknown option '%s'" % name
        close = difflib.get_close_matches(name, list(OPTIONS_BY_NAME), n=3)
        if close:
            message += " (did you mean %s?)" % ", ".join(close)
        raise ValueError(message)

    # Typed accessors.

    def color_spec(self, element):
        override = self.get("color." + element)
        if override:
            return override
        theme = THEME_COLORS.get(self.get("theme"), {})
        return theme.get(element) or THEME_COLORS["default"].get(element, "default")

    def keys(self, action):
        return parse_key_list(self.get("key." + action))

    def key_map(self):
        """Map from canonical key name to the list of actions bound to it."""
        result = {}
        for action, _, _ in ACTIONS:
            for key in self.keys(action):
                result.setdefault(key, []).append(action)
        return result

    def sidebar_panels(self):
        return parse_panel_list(self.get("layout.sidebar-panels"))

    # Persistence.

    def to_json(self):
        return json.dumps(
            dict((k, self.values[k]) for k in sorted(self.values)), indent=2
        )

    def save(self, path=None):
        path = os.path.expanduser(path or default_config_path())
        directory = os.path.dirname(path)
        if directory and not os.path.isdir(directory):
            os.makedirs(directory)
        with open(path, "w") as f:
            f.write(self.to_json() + "\n")
        return path

    def load(self, path=None, ignore_missing=False):
        """Load the options from a JSON file. Returns a list of warnings."""
        path = os.path.expanduser(path or default_config_path())
        if ignore_missing and not os.path.exists(path):
            return []
        with open(path) as f:
            data = json.load(f)
        if not isinstance(data, dict):
            raise ValueError("%s: expected a JSON object" % path)
        warnings = []
        values = {}
        for name, value in data.items():
            try:
                option = self.lookup(name)
                if option.kind in ("bool", "int") and not isinstance(value, str):
                    value = option.format(value)
                values[option.name] = option.parse(str(value))
            except ValueError as e:
                warnings.append("%s: %s" % (path, e))
        self.values.update(values)
        for name, value in list(self.values.items()):
            if value == OPTIONS_BY_NAME[name].default:
                del self.values[name]
        self._changed()
        return warnings


_configs = {}


def get_config(debugger):
    """Return the configuration of the given debugger, creating it (and loading
    the default configuration file) on first use."""
    debugger_id = debugger.GetID()
    config = _configs.get(debugger_id)
    if config is None:
        config = Config()
        _configs[debugger_id] = config
        try:
            config.load_warnings = config.load(ignore_missing=True)
        except (IOError, OSError, ValueError) as e:
            config.load_warnings = ["unable to load %s: %s" % (default_config_path(), e)]
    return config


# ----------------------------------------------------------------------------
# 'tui <subcommand>'
# ----------------------------------------------------------------------------

SUBCOMMAND_HELP = """\
Usage: tui [<subcommand> [<args>]]

Without arguments, switch into the text user interface.

Subcommands:
  tui set <option> <value>     Change an option (e.g. 'tui set theme monokai').
  tui reset [<option>]         Reset an option (or all options) to the default.
  tui show [<pattern>]         Show the options whose name contains <pattern>.
  tui bind <action> <keys>     Bind keys to an action (same as 'tui set
                               key.<action> <keys>'). Keys are comma separated,
                               e.g. 'tui bind step-over F10,n'.
  tui themes                   List the available color themes.
  tui save [<file>]            Save all changed options to <file>.
  tui load [<file>]            Load options from <file>.
  tui help                     Show this help.

The default configuration file is %s and it is loaded automatically.
Options can also be changed interactively in the settings window (F2).
"""


def _split_name_value(args):
    args = args.strip()
    name, _, value = args.partition(" ")
    return name.strip(), value.strip()


def format_option(config, option, width=36):
    value = option.format(config.get(option.name))
    if option.kind == "color" and not value:
        value = "(theme: %s)" % config.color_spec(option.name[len("color."):])
    changed = "*" if option.name in config.values else " "
    return "%s %-*s = %s" % (changed, width, option.name, value)


def run_subcommand(debugger, command, result):
    config = get_config(debugger)
    for warning in getattr(config, "load_warnings", []):
        result.AppendWarning(warning)
    config.load_warnings = []
    subcommand, _, args = command.strip().partition(" ")
    args = args.strip()
    try:
        if subcommand in ("help", "-h", "--help"):
            result.AppendMessage(SUBCOMMAND_HELP % default_config_path())
        elif subcommand == "set":
            name, value = _split_name_value(args)
            if not name or not value:
                raise ValueError("usage: tui set <option> <value>")
            config.set(name, value)
        elif subcommand == "bind":
            name, value = _split_name_value(args)
            if not name:
                raise ValueError("usage: tui bind <action> <keys>")
            option = config.lookup(name if name.startswith("key.") else "key." + name)
            config.set(option.name, value)
        elif subcommand in ("reset", "unset"):
            config.reset(args or None)
        elif subcommand == "show":
            lines = []
            for option in OPTIONS:
                if args in option.name:
                    lines.append(format_option(config, option))
            if not lines:
                raise ValueError("no option matches '%s'" % args)
            result.AppendMessage("\n".join(lines))
        elif subcommand == "themes":
            current = config.get("theme")
            result.AppendMessage(
                "\n".join(("* " if t == current else "  ") + t for t in THEMES)
            )
        elif subcommand == "save":
            path = config.save(args or None)
            result.AppendMessage("Saved TUI configuration to %s" % path)
        elif subcommand == "load":
            for warning in config.load(args or None):
                result.AppendWarning(warning)
        else:
            raise ValueError(
                "unknown subcommand '%s'. Run 'tui help' for a list of "
                "subcommands." % subcommand
            )
    except (ValueError, IOError, OSError) as e:
        result.SetError(str(e))
