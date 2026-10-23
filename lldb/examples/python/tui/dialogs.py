"""Modal windows: text prompts, a filterable picker, the settings window and
the help window."""

from lldb.tui import config as tui_config
from lldb.tui.widgets import TextInput, text_width


class Dialog(object):
    """Base class of modal windows that are drawn on top of the panels."""

    def __init__(self, app, title):
        self.app = app
        self.title = title
        self.done = False
        self.cursor = None

    def color(self, element):
        return self.app.colors.get(element)

    def frame(self, screen, height, width):
        """Draw the window frame centered on the screen and return the inner
        surface."""
        width = min(width, screen.width - 2)
        height = min(height, screen.height - 2)
        y = (screen.height - height) // 2
        x = (screen.width - width) // 2
        surface = screen.sub(y, x, height, width)
        surface.clear(self.color("dialog"))
        surface.box(self.color("border-focus"), self.app.chars)
        surface.put(0, 2, " %s " % self.title, self.color("dialog-title"), width - 4)
        return surface.sub(1, 2, height - 2, width - 4)

    def close(self):
        self.done = True

    def handle_mouse(self, y, x, clicks):
        return True

    def scroll(self, lines):
        return True


class PromptDialog(Dialog):
    def __init__(self, app, title, text, callback, completer=None):
        Dialog.__init__(self, app, title)
        self.input = TextInput(text, history=app.prompt_history, completer=completer)
        self.callback = callback

    def draw(self, screen):
        width = max(50, min(100, text_width(self.title) + 8))
        inner = self.frame(screen, 5, width)
        self.cursor = self.input.draw(inner, 1, 0, inner.width, self.color("input"))

    def handle_key(self, key, name):
        if name == "Enter":
            self.close()
            text = self.input.text
            if text and (not self.app.prompt_history or self.app.prompt_history[-1] != text):
                self.app.prompt_history.append(text)
            self.callback(text)
        elif name in ("Esc", "C-g"):
            self.close()
        else:
            self.input.handle_key(key, name)
        return True


class ConfirmDialog(Dialog):
    """Asks a yes/no question. Enter/y confirms, Esc/n cancels."""

    def __init__(self, app, title, message, callback):
        Dialog.__init__(self, app, title)
        self.message = message
        self.callback = callback

    def draw(self, screen):
        width = max(40, text_width(self.message) + 6, text_width(self.title) + 8)
        inner = self.frame(screen, 6, width)
        inner.put(1, 0, self.message, self.color("dialog"))
        hint = "Enter: yes   Esc: no"
        inner.put(3, max(0, inner.width - text_width(hint)), hint, self.color("dim"))

    def handle_key(self, key, name):
        if name in ("Enter", "y", "Y"):
            self.close()
            self.callback()
        elif name in ("Esc", "n", "N", "q", "C-g"):
            self.close()
        return True

    def handle_mouse(self, y, x, clicks):
        return True


class PickerDialog(Dialog):
    """A list of items that can be filtered by typing."""

    def __init__(self, app, title, items, callback, allow_custom=False):
        Dialog.__init__(self, app, title)
        self.items = items
        self.callback = callback
        self.allow_custom = allow_custom
        self.input = TextInput()
        self.selected = 0
        self.top = 0
        self.filtered = items

    def update_filter(self):
        words = self.input.text.lower().split()
        self.filtered = [i for i in self.items if all(w in i.lower() for w in words)]
        self.selected = 0
        self.top = 0

    def draw(self, screen):
        inner = self.frame(screen, screen.height - 4, max(60, screen.width * 3 // 4))
        self.cursor = self.input.draw(inner, 0, 0, inner.width, self.color("input"), "> ",
                                      self.color("prompt"))
        height = inner.height - 2
        if height <= 0:
            return
        if self.selected < self.top:
            self.top = self.selected
        elif self.selected >= self.top + height:
            self.top = self.selected - height + 1
        for y in range(height):
            index = self.top + y
            if index >= len(self.filtered):
                break
            attr = self.color("selection") if index == self.selected else self.color("dialog")
            inner.fill(y + 2, attr)
            inner.put(y + 2, 1, self.filtered[index], attr)
        if not self.filtered:
            inner.put(2, 1, "No matches" + (" (Enter uses the typed text)"
                                            if self.allow_custom else ""), self.color("dim"))

    def handle_key(self, key, name):
        if name in ("Esc", "C-g"):
            self.close()
        elif name == "Enter":
            if self.filtered:
                choice = self.filtered[self.selected]
            elif self.allow_custom and self.input.text.strip():
                choice = self.input.text.strip()
            else:
                return True
            self.close()
            self.callback(choice)
        elif name in ("Up", "C-p"):
            self.selected = max(0, self.selected - 1)
        elif name in ("Down", "C-n"):
            self.selected = min(len(self.filtered) - 1, self.selected + 1)
        elif name == "PgUp":
            self.selected = max(0, self.selected - 10)
        elif name == "PgDn":
            self.selected = max(0, min(len(self.filtered) - 1, self.selected + 10))
        elif self.input.handle_key(key, name):
            self.update_filter()
        return True

    def handle_mouse(self, y, x, clicks):
        index = self.top + y - 2
        if 0 <= index < len(self.filtered):
            self.selected = index
            if clicks >= 2:
                self.handle_key("\n", "Enter")
        return True

    def scroll(self, lines):
        self.selected = max(0, min(len(self.filtered) - 1, self.selected + lines))
        return True


class SettingsDialog(Dialog):
    """Shows all options grouped by category. Changes are applied immediately."""

    HELP = ("Enter: change  r: reset  R: reset all  s: save  L: load  /: filter  "
            "Esc: close")

    def __init__(self, app):
        Dialog.__init__(self, app, "Settings")
        self.selected = 0
        self.top = 0
        self.filter = ""
        self.rows = []
        self.status = ""
        self.editor = None
        self.editing = None
        self.build_rows()

    @property
    def config(self):
        return self.app.config

    def build_rows(self):
        self.rows = []
        for category, title in tui_config.CATEGORY_TITLES:
            options = [o for o in tui_config.OPTIONS if o.category == category
                       and self.filter.lower() in (o.name + " " + o.description).lower()]
            if not options:
                continue
            self.rows.append(("header", title))
            for option in options:
                self.rows.append(("option", option))
        self.selected = min(self.selected, max(0, len(self.rows) - 1))
        self.skip_headers(1)

    def skip_headers(self, direction):
        while 0 <= self.selected < len(self.rows) and self.rows[self.selected][0] == "header":
            self.selected += direction
        if self.selected >= len(self.rows):
            self.selected = len(self.rows) - 1
            if direction > 0:
                self.skip_headers(-1)
        if self.selected < 0:
            self.selected = 0
            if direction < 0:
                self.skip_headers(1)

    def selected_option(self):
        if 0 <= self.selected < len(self.rows) and self.rows[self.selected][0] == "option":
            return self.rows[self.selected][1]
        return None

    def value_segments(self, option):
        value = self.config.get(option.name)
        if option.kind == "color":
            spec = value or self.config.color_spec(option.name[len("color."):])
            sample = self.app.colors.spec(spec)
            return [(" sample ", sample),
                    (" " + (value if value else "(theme: %s)" % spec), self.color("dialog"))]
        if option.kind == "keys":
            keys = tui_config.parse_key_list(value)
            text = ", ".join(tui_config.format_key(k) for k in keys) or "(unbound)"
            return [(text, self.color("dialog"))]
        text = option.format(value)
        if option.kind == "enum":
            text = "< %s >" % text
        elif option.kind == "bool":
            text = "[x]" if value else "[ ]"
        return [(text, self.color("dialog"))]

    def draw(self, screen):
        inner = self.frame(screen, screen.height - 2, min(screen.width - 2, 120))
        if inner.height < 6:
            return
        name_width = min(36, inner.width // 2)
        list_height = inner.height - 4
        if self.selected < self.top:
            self.top = self.selected
        elif self.selected >= self.top + list_height:
            self.top = self.selected - list_height + 1
        if self.top > 0 and self.rows[self.top - 1][0] == "header" and \
                self.selected - self.top + 1 < list_height:
            self.top -= 1
        for y in range(list_height):
            index = self.top + y
            if index >= len(self.rows):
                break
            kind, item = self.rows[index]
            if kind == "header":
                inner.put(y, 0, item, self.color("title"))
                continue
            attr = self.color("dialog")
            if index == self.selected:
                attr = self.color("selection")
                inner.fill(y, attr)
            changed = "*" if item.name in self.config.values else " "
            inner.put(y, 0, "%s %s" % (changed, item.name), attr, name_width)
            segments = self.value_segments(item)
            if index == self.selected:
                segments = [(t, a if t.startswith(" sample") else attr) for t, a in segments]
            inner.put_segments(y, name_width + 1, segments)

        option = self.selected_option()
        y = inner.height - 4
        inner.put(y, 0, "─" * inner.width if self.app.chars["h"] == "─" else "-" * inner.width,
                  self.color("border"))
        if self.editor is not None:
            label = "New value: "
            self.cursor = self.editor.draw(inner, y + 1, 0, inner.width, self.color("input"),
                                           label, self.color("prompt"))
        else:
            self.cursor = None
            if option:
                description = option.description
                if option.kind == "enum":
                    description += " Values: %s." % ", ".join(option.choices)
                elif option.kind == "keys":
                    description += " Comma separated keys, e.g. 'F5,c', 'C-x', 'S-F11'."
                elif option.kind == "color":
                    description += " Format: fg[,bg[,attr...]]."
                self.draw_wrapped(inner, y + 1, description, 2)
        if self.status:
            inner.put(inner.height - 1, 0, self.status, self.color("stop-reason"))
        else:
            help_text = self.HELP + ("  filter: '%s'" % self.filter if self.filter else "")
            inner.put(inner.height - 1, 0, help_text, self.color("dim"))

    def draw_wrapped(self, inner, y, text, lines):
        words = text.split()
        line = ""
        for word in words:
            if len(line) + len(word) + 1 > inner.width and line:
                inner.put(y, 0, line, self.color("dialog"))
                y += 1
                lines -= 1
                line = ""
                if lines == 0:
                    return
            line = (line + " " + word) if line else word
        if line and lines > 0:
            inner.put(y, 0, line, self.color("dialog"))

    def apply(self, option, value):
        try:
            self.config.set(option.name, value)
            self.status = "%s = %s" % (option.name, option.format(self.config.get(option.name)))
        except ValueError as e:
            self.status = "Error: %s" % e

    def edit(self, option):
        value = self.config.get(option.name)
        if option.kind == "bool":
            self.apply(option, not value)
        elif option.kind == "enum":
            choices = option.choices
            self.apply(option, choices[(choices.index(value) + 1) % len(choices)])
        else:
            self.editor = TextInput(option.format(value))
            self.editing = option

    def handle_key(self, key, name):
        self.status = ""
        if self.editor is not None:
            if name == "Enter":
                editor, self.editor = self.editor, None
                if self.editing is None:
                    self.filter = editor.text
                    self.build_rows()
                else:
                    self.apply(self.editing, editor.text)
            elif name in ("Esc", "C-g"):
                self.editor = None
            else:
                self.editor.handle_key(key, name)
            return True
        option = self.selected_option()
        if name in ("Esc", "q", "F2"):
            self.close()
        elif name in ("Up", "k"):
            self.selected -= 1
            self.skip_headers(-1)
        elif name in ("Down", "j"):
            self.selected += 1
            self.skip_headers(1)
        elif name == "PgUp":
            self.selected -= 10
            self.skip_headers(-1)
        elif name == "PgDn":
            self.selected += 10
            self.skip_headers(1)
        elif name == "Home":
            self.selected = 0
            self.skip_headers(1)
        elif name == "End":
            self.selected = len(self.rows) - 1
            self.skip_headers(-1)
        elif name in ("Enter", " ", "Right", "l") and option:
            self.edit(option)
        elif name in ("Left", "h") and option and option.kind == "enum":
            choices = option.choices
            value = self.config.get(option.name)
            self.apply(option, choices[(choices.index(value) - 1) % len(choices)])
        elif name == "r" and option:
            self.config.reset(option.name)
            self.status = "Reset %s" % option.name
        elif name == "R":
            self.config.reset()
            self.status = "Reset all options to their defaults"
        elif name == "s":
            try:
                self.status = "Saved to %s" % self.config.save()
            except (IOError, OSError) as e:
                self.status = "Error: %s" % e
        elif name == "L":
            try:
                warnings = self.config.load()
                self.status = warnings[0] if warnings else "Loaded %s" % (
                    tui_config.default_config_path())
            except (IOError, OSError, ValueError) as e:
                self.status = "Error: %s" % e
        elif name == "/":
            self.editor = TextInput(self.filter)
            self.editing = None
        return True

    def handle_mouse(self, y, x, clicks):
        index = self.top + y
        if 0 <= index < len(self.rows) and self.rows[index][0] == "option":
            self.selected = index
            if clicks >= 2:
                self.edit(self.rows[index][1])
        return True

    def scroll(self, lines):
        self.selected = max(0, min(len(self.rows) - 1, self.selected + lines))
        self.skip_headers(1 if lines > 0 else -1)
        return True


HELP_INTRO = """\
The LLDB text user interface shows the state of the debugged program in
several panels. The focused panel has a highlighted border; use the focus keys
or the mouse to change the focus. Commands can be entered in the console at the
bottom. Everything can be configured in the settings window (%s) or with
'tui set' in an .lldbinit file (run 'tui help' in LLDB for details).

Console keys: Enter runs the command (an empty line repeats the previous one),
Tab completes (on an empty line it focuses the next panel), Up/Down browse the
history, PgUp/PgDn scroll the output, C-t switches between LLDB commands and
sending input to the process, Esc leaves the console.
"""


class HelpDialog(Dialog):
    def __init__(self, app):
        Dialog.__init__(self, app, "Help")
        self.top = 0
        self.lines = []

    def build(self, width):
        config = self.app.config
        settings_keys = ", ".join(tui_config.format_key(k) for k in config.keys("settings"))
        lines = [(line, "dialog") for line in (HELP_INTRO % (settings_keys or "unbound")).splitlines()]
        lines.append(("", "dialog"))
        lines.append(("Key bindings", "title"))
        for action, _, description in tui_config.ACTIONS:
            keys = ", ".join(tui_config.format_key(k) for k in config.keys(action))
            lines.append(("  %-24s %-16s %s" % (action, keys or "(unbound)", description),
                          "dialog"))
        return lines

    def draw(self, screen):
        inner = self.frame(screen, screen.height - 2, min(screen.width - 2, 130))
        self.lines = self.build(inner.width)
        height = inner.height - 1
        self.top = max(0, min(self.top, len(self.lines) - height))
        for y in range(height):
            index = self.top + y
            if index >= len(self.lines):
                break
            text, element = self.lines[index]
            inner.put(y, 0, text, self.color(element))
        inner.put(inner.height - 1, 0, "Up/Down/PgUp/PgDn: scroll  Esc: close",
                  self.color("dim"))

    def handle_key(self, key, name):
        if name in ("Esc", "q", "F1", "?", "Enter"):
            self.close()
        elif name in ("Up", "k"):
            self.top -= 1
        elif name in ("Down", "j"):
            self.top += 1
        elif name == "PgUp":
            self.top -= 10
        elif name in ("PgDn", " "):
            self.top += 10
        self.top = max(0, self.top)
        return True

    def scroll(self, lines):
        self.top = max(0, self.top + lines)
        return True
