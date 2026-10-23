"""Basic building blocks of the interface: colors, drawing, key handling, a
single line text input and a scrollable list panel."""

try:
    import curses
except ImportError:  # e.g. Windows without the curses module.
    curses = None

import unicodedata

from lldb.tui import config as tui_config

# ----------------------------------------------------------------------------
# Colors
# ----------------------------------------------------------------------------


class Colors(object):
    """Turns the color specifications of the configuration into curses
    attributes. Color pairs are allocated on demand."""

    def __init__(self, config):
        self.config = config
        self.pairs = {}
        self.cache = {}
        self.has_colors = False
        self.num_colors = 0
        self.num_pairs = 0

    def init(self):
        self.has_colors = curses.has_colors()
        if self.has_colors:
            curses.start_color()
            try:
                curses.use_default_colors()
                self.default_bg = -1
            except curses.error:
                self.default_bg = curses.COLOR_BLACK
            self.num_colors = curses.COLORS
            self.num_pairs = curses.COLOR_PAIRS
        self.reset()

    def reset(self):
        self.cache = {}
        self.pairs = {}

    def _fit(self, color):
        """Map a color to one the terminal supports."""
        if color < 0 or color < self.num_colors:
            return color, 0
        if color < 16:
            # Bright colors on 8 color terminals: use the normal color + bold.
            return color - 8, curses.A_BOLD
        # 256 color palette on a terminal with fewer colors: approximate.
        if color >= 232:
            return (curses.COLOR_WHITE if color >= 244 else curses.COLOR_BLACK), 0
        color -= 16
        r, g, b = color // 36, (color // 6) % 6, color % 6
        value = (1 if r >= 3 else 0) | (2 if g >= 3 else 0) | (4 if b >= 3 else 0)
        return value, 0

    def _pair(self, fg, bg):
        key = (fg, bg)
        if key in self.pairs:
            return self.pairs[key]
        number = len(self.pairs) + 1
        if number >= self.num_pairs:
            return 0
        try:
            curses.init_pair(number, fg, bg)
        except curses.error:
            return 0
        self.pairs[key] = curses.color_pair(number)
        return self.pairs[key]

    def get(self, element):
        """The attribute of a color element of the configuration."""
        attr = self.cache.get(element)
        if attr is None:
            attr = self.spec(self.config.color_spec(element))
            self.cache[element] = attr
        return attr

    def spec(self, spec):
        """The attribute of a color specification ('fg[,bg[,attr...]]')."""
        key = "=" + spec
        attr = self.cache.get(key)
        if attr is not None:
            return attr
        try:
            fg, bg, attrs = tui_config.parse_color_spec(spec)
        except ValueError:
            fg, bg, attrs = -1, -1, []
        attr = 0
        for name in attrs:
            attr |= getattr(curses, "A_" + name.upper(), 0)
        if self.has_colors and (fg != -1 or bg != -1):
            fg, extra_fg = self._fit(fg)
            bg, _ = self._fit(bg)
            if bg == -1:
                bg = self.default_bg
            attr |= extra_fg | self._pair(fg, bg)
        elif not self.has_colors and bg not in (-1,):
            # A background color is used to highlight something. Without colors
            # reverse video is the best approximation.
            attr |= curses.A_REVERSE
        self.cache[key] = attr
        return attr


# ----------------------------------------------------------------------------
# Drawing
# ----------------------------------------------------------------------------

BORDERS = {
    "unicode": {
        "h": "─", "v": "│", "tl": "┌", "tr": "┐",
        "bl": "└", "br": "┘", "expanded": "▾",
        "collapsed": "▸", "pc": "▶", "pc-other": "▷",
        "breakpoint": "●", "breakpoint-disabled": "○",
        "frame": "→", "ellipsis": "…", "scroll": "█",
    },
    "ascii": {
        "h": "-", "v": "|", "tl": "+", "tr": "+", "bl": "+", "br": "+",
        "expanded": "-", "collapsed": "+", "pc": ">", "pc-other": ")",
        "breakpoint": "*", "breakpoint-disabled": "o", "frame": ">",
        "ellipsis": "~", "scroll": "#",
    },
}


def char_width(char):
    if unicodedata.east_asian_width(char) in ("W", "F"):
        return 2
    if unicodedata.combining(char):
        return 0
    return 1


def text_width(text):
    if text.isascii():
        return len(text)
    return sum(char_width(c) for c in text)


def clip(text, width):
    """Clip text to the given number of columns."""
    if width <= 0:
        return ""
    if len(text) <= width and text_width(text) <= width:
        return text
    result = []
    used = 0
    for char in text:
        w = char_width(char)
        if used + w > width:
            break
        result.append(char)
        used += w
    return "".join(result)


def sanitize(text):
    """Replace control characters that would confuse curses."""
    if not any(ord(c) < 32 or ord(c) == 127 for c in text):
        return text
    return "".join(
        c if (ord(c) >= 32 and ord(c) != 127) else ("^" + chr((ord(c) + 64) % 128))
        for c in text
    )


class Surface(object):
    """A rectangular region of the screen. All drawing is clipped to it."""

    def __init__(self, screen, y, x, height, width):
        self.screen = screen
        self.y = y
        self.x = x
        self.height = max(0, height)
        self.width = max(0, width)

    def sub(self, y, x, height, width):
        height = min(height, self.height - y)
        width = min(width, self.width - x)
        return Surface(self.screen, self.y + y, self.x + x, height, width)

    def put(self, y, x, text, attr=0, width=None):
        """Draw text at (y, x) relative to the surface. Returns the number of
        columns that were drawn."""
        if y < 0 or y >= self.height or x >= self.width or not text:
            return 0
        limit = self.width - x
        if width is not None:
            limit = min(limit, width)
        text = clip(sanitize(text), limit)
        if not text:
            return 0
        try:
            self.screen.addstr(self.y + y, self.x + x, text, attr)
        except curses.error:
            # Writing the bottom right cell of the screen moves the cursor out
            # of the screen, which curses reports as error after drawing.
            pass
        return text_width(text)

    def put_segments(self, y, x, segments, width=None, base_attr=0):
        """Draw a list of (text, attr) segments. Returns the end column."""
        limit = self.width if width is None else min(self.width, x + width)
        for text, attr in segments:
            if x >= limit:
                break
            x += self.put(y, x, text, attr | base_attr, limit - x)
        return x

    def fill(self, y, attr=0, x=0, width=None):
        if width is None:
            width = self.width - x
        if width > 0:
            self.put(y, x, " " * width, attr)

    def clear(self, attr=0):
        for y in range(self.height):
            self.fill(y, attr)

    def box(self, attr, chars):
        if self.height < 2 or self.width < 2:
            return
        h = chars["h"] * (self.width - 2)
        self.put(0, 0, chars["tl"] + h + chars["tr"], attr)
        for y in range(1, self.height - 1):
            self.put(y, 0, chars["v"], attr)
            self.put(y, self.width - 1, chars["v"], attr)
        self.put(self.height - 1, 0, chars["bl"] + h + chars["br"], attr)


# ----------------------------------------------------------------------------
# Keys
# ----------------------------------------------------------------------------


def key_name(key):
    """Convert a key returned by get_wch() into the canonical key name that the
    configuration uses (see config.parse_key)."""
    if isinstance(key, str):
        if key in ("\n", "\r"):
            return "Enter"
        if key == "\t":
            return "Tab"
        if key == "\x1b":
            return "Esc"
        if key in ("\x7f", "\x08"):
            return "Backspace"
        if len(key) == 1 and ord(key) < 32:
            return "C-" + chr(ord(key) + 96)
        return key
    names = {
        curses.KEY_UP: "Up",
        curses.KEY_DOWN: "Down",
        curses.KEY_LEFT: "Left",
        curses.KEY_RIGHT: "Right",
        curses.KEY_PPAGE: "PgUp",
        curses.KEY_NPAGE: "PgDn",
        curses.KEY_HOME: "Home",
        curses.KEY_END: "End",
        curses.KEY_DC: "Delete",
        curses.KEY_IC: "Insert",
        curses.KEY_BACKSPACE: "Backspace",
        curses.KEY_ENTER: "Enter",
        curses.KEY_BTAB: "S-Tab",
        curses.KEY_RESIZE: "Resize",
        curses.KEY_MOUSE: "Mouse",
    }
    for extra, name in (("KEY_A1", "Home"), ("KEY_C1", "End"),
                        ("KEY_A3", "PgUp"), ("KEY_C3", "PgDn")):
        if hasattr(curses, extra):
            names.setdefault(getattr(curses, extra), name)
    if key in names:
        return names[key]
    if curses.KEY_F0 < key <= curses.KEY_F0 + 24:
        return "F%d" % (key - curses.KEY_F0)
    return "<%d>" % key


# ----------------------------------------------------------------------------
# Text input
# ----------------------------------------------------------------------------


class TextInput(object):
    """A single line editor with emacs style key bindings, history and an
    optional completion callback."""

    def __init__(self, text="", history=None, completer=None):
        self.text = text
        self.cursor = len(text)
        self.scroll = 0
        self.history = history if history is not None else []
        self.history_index = None
        self.saved_text = ""
        self.completer = completer
        self.kill_buffer = ""

    def set_text(self, text):
        self.text = text
        self.cursor = len(text)
        self.history_index = None

    def handle_key(self, key, name):
        """Returns True if the key was handled."""
        text, cursor = self.text, self.cursor
        if isinstance(key, str) and len(key) == 1 and ord(key) >= 32 and key != "\x7f":
            self.text = text[:cursor] + key + text[cursor:]
            self.cursor += 1
        elif name in ("Backspace", "C-h"):
            if cursor > 0:
                self.text = text[: cursor - 1] + text[cursor:]
                self.cursor -= 1
        elif name in ("Delete", "C-d"):
            self.text = text[:cursor] + text[cursor + 1 :]
        elif name in ("Left", "C-b"):
            self.cursor = max(0, cursor - 1)
        elif name in ("Right", "C-f"):
            self.cursor = min(len(text), cursor + 1)
        elif name in ("Home", "C-a"):
            self.cursor = 0
        elif name in ("End", "C-e"):
            self.cursor = len(text)
        elif name == "C-k":
            self.kill_buffer = text[cursor:]
            self.text = text[:cursor]
        elif name == "C-u":
            self.kill_buffer = text[:cursor]
            self.text = text[cursor:]
            self.cursor = 0
        elif name == "C-w":
            start = cursor
            while start > 0 and text[start - 1] == " ":
                start -= 1
            while start > 0 and text[start - 1] != " ":
                start -= 1
            self.kill_buffer = text[start:cursor]
            self.text = text[:start] + text[cursor:]
            self.cursor = start
        elif name == "C-y":
            self.text = text[:cursor] + self.kill_buffer + text[cursor:]
            self.cursor += len(self.kill_buffer)
        elif name in ("Up", "C-p"):
            self.history_move(-1)
        elif name in ("Down", "C-n"):
            self.history_move(1)
        elif name == "Tab" and self.completer:
            self.completer(self)
        else:
            return False
        return True

    def history_move(self, delta):
        if not self.history:
            return
        if self.history_index is None:
            if delta > 0:
                return
            self.saved_text = self.text
            index = len(self.history) - 1
        else:
            index = self.history_index + delta
        if index >= len(self.history):
            self.history_index = None
            self.text = self.saved_text
        else:
            self.history_index = max(0, index)
            self.text = self.history[self.history_index]
        self.cursor = len(self.text)

    def draw(self, surface, y, x, width, attr=0, prompt="", prompt_attr=0,
             focused=True):
        """Draw the input. Returns the screen position of the cursor."""
        x += surface.put(y, x, prompt, prompt_attr, width)
        width -= text_width(prompt)
        if width <= 1:
            return None
        if self.cursor < self.scroll:
            self.scroll = self.cursor
        if self.cursor - self.scroll >= width:
            self.scroll = self.cursor - width + 1
        visible = self.text[self.scroll : self.scroll + width]
        surface.put(y, x, visible + " " * (width - text_width(visible)), attr, width)
        if not focused:
            return None
        return (surface.y + y, surface.x + x + text_width(self.text[self.scroll : self.cursor]))


# ----------------------------------------------------------------------------
# Panels
# ----------------------------------------------------------------------------


class Pane(object):
    """Base class of all panels.

    A panel has a name (used for focusing it and in the configuration), a title
    that is drawn in its border and a surface that is assigned by the layout.
    """

    name = ""
    title = ""

    def __init__(self, app):
        self.app = app
        self.surface = None
        self.inner = None

    @property
    def config(self):
        return self.app.config

    def color(self, element):
        return self.app.colors.get(element)

    def layout(self, surface, border=True):
        self.surface = surface
        if border:
            self.inner = surface.sub(1, 1, surface.height - 2, surface.width - 2)
        else:
            self.inner = surface

    def title_text(self):
        return self.title

    # Cleared by the layout when the title is shown elsewhere (tabbed mode).
    show_title = True

    def draw_frame(self, focused):
        surface = self.surface
        chars = self.app.chars
        surface.box(self.color("border-focus" if focused else "border"), chars)
        if self.show_title:
            title = " %s " % self.title_text()
            surface.put(0, 2, title, self.color("title-focus" if focused else "title"),
                        surface.width - 4)

    def draw(self, focused):
        raise NotImplementedError

    def refresh(self):
        """Called when the state of the debugger changed."""

    def handle_action(self, action, key, name):
        """Handle an action that is bound to the pressed key. Returns True if
        the action was handled."""
        return False

    def handle_key(self, key, name):
        """Handle a key that isn't bound to an action."""
        return False

    def handle_mouse(self, y, x, clicks):
        """Handle a click at (y, x) relative to the inner surface."""
        return False

    def scroll(self, lines):
        return False

    def cursor_position(self):
        return None


class ListPane(Pane):
    """A panel that shows a list of rows of which one is selected."""

    empty_text = ""

    def __init__(self, app):
        Pane.__init__(self, app)
        self.rows = []
        self.selected = 0
        self.top = 0

    @property
    def rows(self):
        return self._rows

    @rows.setter
    def rows(self, rows):
        # The segments of the rows are computed once when the rows change and
        # not on every redraw. Computing them queries the debugger, which
        # mustn't happen while the process is running (e.g. during a step):
        # the panel keeps showing the last stop until the process stops again.
        self._rows = rows
        self._segments = {}

    def row_segments(self, row, width):
        """Return the list of (text, attr) segments for a row."""
        raise NotImplementedError

    def cached_row_segments(self, index):
        segments = self._segments.get(index)
        if segments is None:
            segments = self.row_segments(self._rows[index], self.inner.width)
            self._segments[index] = segments
        return segments

    def row_attr(self, row):
        return 0

    def activate(self, row):
        pass

    def visible_rows(self):
        return max(1, self.inner.height) if self.inner else 1

    def ensure_visible(self):
        height = self.visible_rows()
        if self.selected < self.top:
            self.top = self.selected
        elif self.selected >= self.top + height:
            self.top = self.selected - height + 1
        self.top = max(0, min(self.top, max(0, len(self.rows) - height)))

    def select(self, index):
        if not self.rows:
            self.selected = 0
            return
        self.selected = max(0, min(index, len(self.rows) - 1))
        self.ensure_visible()

    def selected_row(self):
        if 0 <= self.selected < len(self.rows):
            return self.rows[self.selected]
        return None

    def draw(self, focused):
        self.draw_frame(focused)
        inner = self.inner
        if inner.height <= 0 or inner.width <= 0:
            return
        if not self.rows:
            inner.put(0, 1, self.empty_text, self.color("dim"))
            return
        self.ensure_visible()
        for y in range(inner.height):
            index = self.top + y
            if index >= len(self.rows):
                break
            row = self.rows[index]
            base = self.row_attr(row)
            if index == self.selected:
                base = self.color("selection" if focused else "selection-unfocused")
                segments = [(text, base) for text, _ in self.cached_row_segments(index)]
                inner.fill(y, base)
            else:
                segments = self.cached_row_segments(index)
            inner.put_segments(y, 0, segments)
        self.draw_scrollbar()

    def draw_scrollbar(self):
        inner = self.inner
        total = len(self.rows)
        if total <= inner.height or inner.height < 3:
            return
        bar = max(1, inner.height * inner.height // total)
        start = (inner.height - bar) * self.top // max(1, total - inner.height)
        for y in range(bar):
            self.surface.put(1 + start + y, self.surface.width - 1,
                             self.app.chars["scroll"], self.color("border"))

    def handle_action(self, action, key, name):
        page = max(1, self.visible_rows() - 1)
        if action == "up":
            self.select(self.selected - 1)
        elif action == "down":
            self.select(self.selected + 1)
        elif action == "page-up":
            self.select(self.selected - page)
        elif action == "page-down":
            self.select(self.selected + page)
        elif action == "top":
            self.select(0)
        elif action == "bottom":
            self.select(len(self.rows) - 1)
        elif action == "activate":
            row = self.selected_row()
            if row is not None:
                self.activate(row)
        else:
            return False
        return True

    def handle_mouse(self, y, x, clicks):
        index = self.top + y
        if 0 <= index < len(self.rows):
            self.select(index)
            if clicks >= 2:
                self.activate(self.rows[index])
            return True
        return False

    def scroll(self, lines):
        self.top = max(0, min(self.top + lines, max(0, len(self.rows) - self.visible_rows())))
        if self.selected < self.top:
            self.selected = self.top
        elif self.selected >= self.top + self.visible_rows():
            self.selected = self.top + self.visible_rows() - 1
        return True
