"""The panels of the interface."""

import os
import re

import lldb

from lldb.tui import highlight
from lldb.tui.widgets import ListPane, Pane, TextInput, clip, text_width

# ----------------------------------------------------------------------------
# Helpers
# ----------------------------------------------------------------------------


def file_spec_path(file_spec):
    if not file_spec or not file_spec.IsValid():
        return None
    return file_spec.fullpath


def short_path(path):
    return os.path.basename(path) if path else "?"


def describe_line_entry(line_entry):
    if not line_entry or not line_entry.IsValid():
        return None
    path = file_spec_path(line_entry.GetFileSpec())
    if not path or line_entry.GetLine() == 0:
        return None
    return "%s:%d" % (short_path(path), line_entry.GetLine())


def overlay(segments, ranges, attr):
    """Apply attr to the character ranges [(start, end), ...] of the text formed
    by the segments."""
    if not ranges:
        return segments
    result = []
    position = 0
    for text, seg_attr in segments:
        start = position
        end = position + len(text)
        cuts = [start, end]
        for r_start, r_end in ranges:
            if r_end > start and r_start < end:
                cuts.extend([max(start, r_start), min(end, r_end)])
        cuts = sorted(set(cuts))
        for a, b in zip(cuts, cuts[1:]):
            piece = text[a - start : b - start]
            inside = any(r_start <= a and b <= r_end for r_start, r_end in ranges)
            result.append((piece, attr if inside else seg_attr))
        position = end
    return result


def hscroll_segments(segments, offset):
    """Drop the first offset characters of a segment list."""
    if offset <= 0:
        return segments
    result = []
    for text, attr in segments:
        if offset >= len(text):
            offset -= len(text)
            continue
        result.append((text[offset:], attr))
        offset = 0
    return result


def value_text(value):
    """The text that is shown for the value of an SBValue."""
    if not value or not value.IsValid():
        return "", True
    error = value.GetError()
    text = value.GetValue() or ""
    summary = value.GetSummary() or ""
    if summary:
        text = (text + " " + summary) if text else summary
    if not text and error.Fail():
        return error.GetCString() or "<error>", False
    return text, True


# ----------------------------------------------------------------------------
# Source and disassembly
# ----------------------------------------------------------------------------


class SourceFile(object):
    def __init__(self, path, lines, mtime):
        self.path = path
        self.lines = lines
        self.mtime = mtime
        self.tokens = None
        self.tab_width = None


class SourcePane(Pane):
    name = "source"
    title = "Source"

    def __init__(self, app):
        Pane.__init__(self, app)
        self.files = {}
        self.file = None
        self.mode = "source"
        self.forced_mode = None
        self.cursor = 0
        # The column of the cursor in the tab-expanded line and the column
        # the cursor tries to stay in when moving between lines.
        self.column = 0
        self.goal_column = 0
        self.top = 0
        self.hscroll = 0
        # Where the text starts and how wide it is (set when drawing).
        self.text_x = 0
        self.text_width = 1
        self.location_key = None
        self.pc_lines = {}
        self.breakpoint_lines = {}
        self.instructions = []
        self.disassembly_key = None
        self.disassembly_title = ""
        self.search_text = ""
        self.message = None

    # Loading.

    def resolve_path(self, path):
        if not path:
            return None
        if os.path.isfile(path):
            return path
        # Apply target.source-map.
        try:
            data = self.app.debugger.GetSetting("target.source-map")
            entries = []
            for i in range(data.GetSize()):
                entries.append(data.GetItemAtIndex(i).GetStringValue(4096))
            for i in range(0, len(entries) - 1, 2):
                prefix, replacement = entries[i], entries[i + 1]
                if prefix and path.startswith(prefix):
                    candidate = replacement + path[len(prefix) :]
                    if os.path.isfile(candidate):
                        return candidate
        except Exception:
            pass
        return None

    def load_file(self, path):
        resolved = self.resolve_path(path)
        if not resolved:
            return None
        try:
            mtime = os.path.getmtime(resolved)
        except OSError:
            return None
        cached = self.files.get(resolved)
        if cached and cached.mtime == mtime:
            return cached
        try:
            with open(resolved, "rb") as f:
                data = f.read()
        except (IOError, OSError):
            return None
        text = data.decode("utf-8", errors="replace")
        lines = text.splitlines() or [""]
        source = SourceFile(resolved, lines, mtime)
        self.files[resolved] = source
        return source

    def tokens(self):
        """Return the expanded lines and their tokens of the current file."""
        source = self.file
        tab_width = self.config.get("source.tab-width")
        if source.tokens is None or source.tab_width != tab_width:
            lines = [line.expandtabs(tab_width) for line in source.lines]
            language = highlight.language_for_file(source.path)
            if language is None:
                compile_unit = self.app.frame_compile_unit()
                if compile_unit:
                    language = highlight.language_by_name(
                        lldb.SBLanguageRuntime.GetNameForLanguageType(
                            compile_unit.GetLanguage()
                        )
                    )
            if language is not None:
                source.tokens = highlight.highlight_lines(language, lines)
            else:
                source.tokens = [[(line, "text")] for line in lines]
            source.tab_width = tab_width
        return source.tokens

    def show_file(self, path, line=None, focus_line=True):
        source = self.load_file(path)
        if not source:
            self.app.message("Unable to read source file '%s'" % path, "console-error")
            return False
        self.file = source
        self.mode = "source"
        if line is not None and focus_line:
            self.cursor = max(0, min(line - 1, len(source.lines) - 1))
            self.set_column(self.indent_column(self.cursor))
            self.center_cursor()
        self.update_markers()
        return True

    def indent_column(self, index):
        line = self.expanded_line(index)
        return len(line) - len(line.lstrip())

    def load_disassembly(self, frame):
        target = self.app.target
        function = frame.GetFunction()
        symbol = frame.GetSymbol()
        if function.IsValid():
            key = ("function", function.GetStartAddress().GetLoadAddress(target))
            start = function.GetStartAddress().GetLoadAddress(target)
            name = function.GetDisplayName() or function.GetName()
            getter = lambda: function.GetInstructions(target)
        elif symbol.IsValid():
            key = ("symbol", symbol.GetStartAddress().GetLoadAddress(target))
            start = symbol.GetStartAddress().GetLoadAddress(target)
            name = symbol.GetDisplayName() or symbol.GetName()
            getter = lambda: symbol.GetInstructions(target)
        else:
            pc = frame.GetPC()
            key = ("address", pc)
            start = pc
            name = "0x%x" % pc
            getter = lambda: target.ReadInstructions(frame.GetPCAddress(), 128)
        if key == self.disassembly_key and self.instructions:
            return
        instructions = []
        for instruction in getter():
            address = instruction.GetAddress().GetLoadAddress(target)
            data = b""
            if self.config.get("source.show-bytes"):
                error = lldb.SBError()
                sbdata = instruction.GetData(target)
                data = sbdata.ReadRawData(error, 0, sbdata.GetByteSize()) or b""
            instructions.append(
                {
                    "address": address,
                    "offset": address - start if start != lldb.LLDB_INVALID_ADDRESS else 0,
                    "mnemonic": instruction.GetMnemonic(target) or "",
                    "operands": instruction.GetOperands(target) or "",
                    "comment": instruction.GetComment(target) or "",
                    "bytes": data,
                }
            )
        self.instructions = instructions
        self.disassembly_key = key
        self.disassembly_title = name

    # State updates.

    def refresh(self):
        frame = self.app.frame()
        self.message = None
        if frame is None:
            self.pc_lines = {}
            self.location_key = None
            if self.mode == "disassembly":
                self.mode = "source"
            if not self.file:
                self.message = self.app.idle_message()
            self.update_markers()
            return

        line_entry = frame.GetLineEntry()
        path = file_spec_path(line_entry.GetFileSpec()) if line_entry.IsValid() else None
        line = line_entry.GetLine() if line_entry.IsValid() else 0
        key = (self.app.stop_id, frame.GetThread().GetIndexID(), frame.GetFrameID(),
               self.forced_mode)
        location_changed = key != self.location_key
        self.location_key = key

        want = self.config.get("source.disassembly")
        if self.forced_mode:
            want = "always" if self.forced_mode == "disassembly" else "never"
        source = self.load_file(path) if path and line else None
        use_disassembly = want == "always" or (want == "auto" and not source)
        if want == "never" and not source:
            use_disassembly = False

        if use_disassembly:
            self.load_disassembly(frame)
            if location_changed or self.mode != "disassembly":
                self.mode = "disassembly"
                pc = frame.GetPC()
                for index, instruction in enumerate(self.instructions):
                    if instruction["address"] == pc:
                        self.cursor = index
                        break
                self.center_cursor()
        elif source:
            if location_changed or self.mode != "source":
                self.mode = "source"
                self.file = source
                self.cursor = max(0, min(line - 1, len(source.lines) - 1))
                column = line_entry.GetColumn()
                self.set_column(column - 1 if column else self.indent_column(self.cursor))
                self.center_cursor()
        else:
            if location_changed:
                self.file = None
                self.message = "No source available for %s" % (
                    frame.GetDisplayFunctionName() or "0x%x" % frame.GetPC())
        self.update_markers()

    def update_markers(self):
        """Compute the program counter and breakpoint markers."""
        self.pc_lines = {}
        self.breakpoint_lines = {}
        target = self.app.target
        if not target or not target.IsValid():
            return
        disassembly = self.mode == "disassembly"
        current_path = self.file.path if self.file else None

        def location_key(address):
            if disassembly:
                return address.GetLoadAddress(target)
            line_entry = address.GetLineEntry()
            if not line_entry.IsValid():
                return None
            path = self.resolve_path(file_spec_path(line_entry.GetFileSpec()))
            if path != current_path or not path:
                return None
            return line_entry.GetLine()

        if self.app.is_stopped():
            process = self.app.process
            selected = process.GetSelectedThread()
            for thread in process:
                frame = thread.GetFrameAtIndex(0)
                if not frame.IsValid():
                    continue
                key = location_key(frame.GetPCAddress())
                if key is None:
                    continue
                is_selected = thread.GetIndexID() == selected.GetIndexID()
                if is_selected or key not in self.pc_lines:
                    self.pc_lines[key] = "pc" if is_selected else "pc-other"
            frame = self.app.frame()
            if frame is not None and frame.GetFrameID() != 0:
                key = location_key(frame.GetPCAddress())
                if key is not None:
                    self.pc_lines[key] = "frame"

        for breakpoint in target.breakpoint_iter():
            for location in breakpoint:
                key = location_key(location.GetAddress())
                if key is None:
                    continue
                enabled = breakpoint.IsEnabled() and location.IsEnabled()
                if enabled or key not in self.breakpoint_lines:
                    self.breakpoint_lines[key] = enabled

    # Navigation.

    def line_count(self):
        if self.mode == "disassembly":
            return len(self.instructions)
        return len(self.file.lines) if self.file else 0

    def center_cursor(self):
        height = self.inner.height if self.inner else 20
        self.top = max(0, self.cursor - height // 2)

    def ensure_visible(self):
        if not self.inner:
            return
        height = max(1, self.inner.height)
        margin = min(self.config.get("source.scroll-margin"), (height - 1) // 2)
        if self.cursor < self.top + margin:
            self.top = self.cursor - margin
        elif self.cursor >= self.top + height - margin:
            self.top = self.cursor - height + margin + 1
        self.top = max(0, min(self.top, max(0, self.line_count() - height)))

    def move_cursor(self, line):
        count = self.line_count()
        if count == 0:
            return
        self.cursor = max(0, min(line, count - 1))
        self.set_column(self.goal_column, keep_goal=True)
        self.ensure_visible()

    def expanded_line(self, index):
        if self.mode != "source" or not self.file or not 0 <= index < len(self.file.lines):
            return ""
        return self.file.lines[index].expandtabs(self.config.get("source.tab-width"))

    def set_column(self, column, keep_goal=False):
        """Move the column cursor, keeping it within the current line."""
        if self.mode != "source":
            return
        length = len(self.expanded_line(self.cursor))
        if not keep_goal:
            self.goal_column = max(0, column)
        self.column = max(0, min(column, length - 1 if length else 0))
        if self.column < self.hscroll:
            self.hscroll = self.column
        elif self.column >= self.hscroll + self.text_width:
            self.hscroll = self.column - self.text_width + 1

    def source_column(self):
        """The 1-based column of the cursor in the line as it is in the file
        (tabs count as one column there)."""
        line = self.file.lines[self.cursor]
        tab_width = self.config.get("source.tab-width")
        display = 0
        for index, char in enumerate(line):
            width = tab_width - display % tab_width if char == "\t" else 1
            if display + width > self.column:
                return index + 1
            display += width
        return len(line) + 1

    def scroll(self, lines):
        count = self.line_count()
        height = self.inner.height if self.inner else 1
        self.top = max(0, min(self.top + lines, max(0, count - height)))
        self.cursor = max(self.top, min(self.cursor, self.top + height - 1))
        return True

    # Drawing.

    def title_text(self):
        if self.mode == "disassembly":
            return "Disassembly: %s" % self.disassembly_title
        if self.file:
            return "Source: %s:%d:%d" % (self.file.path, self.cursor + 1,
                                         self.source_column())
        return "Source"

    def draw(self, focused):
        self.draw_frame(focused)
        inner = self.inner
        if inner.height <= 0 or inner.width <= 0:
            return
        if self.mode == "disassembly":
            self.draw_disassembly(focused)
        elif self.file:
            self.draw_source(focused)
        else:
            message = self.message or self.app.idle_message()
            for i, line in enumerate(message.splitlines()):
                inner.put(1 + i, 2, line, self.color("dim"))

    def gutter(self, key, line_label, label_width, cursor_line, focused):
        chars = self.app.chars
        segments = []
        if key in self.breakpoint_lines:
            enabled = self.breakpoint_lines[key]
            segments.append((chars["breakpoint" if enabled else "breakpoint-disabled"],
                             self.color("breakpoint" if enabled else "breakpoint-disabled")))
        else:
            segments.append((" ", 0))
        marker = self.pc_lines.get(key)
        if marker:
            symbol = {"pc": chars["pc"], "pc-other": chars["pc-other"],
                      "frame": chars["frame"]}[marker]
            segments.append((symbol, self.color("pc-marker")))
        else:
            segments.append((" ", 0))
        if label_width:
            attr = self.color("line-number")
            if cursor_line:
                attr = self.color("selection" if focused else "selection-unfocused")
            segments.append((line_label.rjust(label_width), attr))
        segments.append((" ", 0))
        return segments, marker

    def search_ranges(self, text):
        if not self.search_text:
            return []
        ranges = []
        lowered = text.lower()
        needle = self.search_text.lower()
        start = lowered.find(needle)
        while start >= 0 and needle:
            ranges.append((start, start + len(needle)))
            start = lowered.find(needle, start + len(needle))
        return ranges

    def line_attr(self, marker):
        if marker and self.config.get("source.highlight-pc-line"):
            return self.color("frame-line" if marker == "frame" else "pc-line")
        return None

    def draw_source(self, focused):
        inner = self.inner
        tokens = self.tokens()
        highlight_syntax = self.config.get("source.syntax-highlight")
        label_width = len(str(len(tokens))) if self.config.get("source.line-numbers") else 0
        self.ensure_visible()
        for y in range(inner.height):
            index = self.top + y
            if index >= len(tokens):
                break
            line_number = index + 1
            gutter, marker = self.gutter(line_number, str(line_number), label_width,
                                         index == self.cursor, focused)
            x = inner.put_segments(y, 0, gutter)
            line_tokens = tokens[index]
            if highlight_syntax:
                segments = [(text, self.color(kind)) for text, kind in line_tokens]
            else:
                segments = [("".join(t for t, _ in line_tokens), self.color("text"))]
            full_attr = self.line_attr(marker)
            if full_attr is not None:
                segments = [(text, full_attr) for text, _ in segments]
                inner.fill(y, full_attr, x)
            text = "".join(t for t, _ in segments)
            segments = overlay(segments, self.search_ranges(text), self.color("search-match"))
            if index == self.cursor and focused:
                if not text:
                    segments = [(" ", 0)]
                segments = overlay(segments, [(self.column, self.column + 1)],
                                   self.color("selection"))
            inner.put_segments(y, x, hscroll_segments(segments, self.hscroll))
            self.text_x = x
        self.text_width = max(1, inner.width - self.text_x)

    def cursor_position(self):
        if self.mode != "source" or not self.file or not self.inner:
            return None
        y = self.cursor - self.top
        x = self.text_x + self.column - self.hscroll
        if not (0 <= y < self.inner.height and 0 <= x < self.inner.width):
            return None
        return (self.inner.y + y, self.inner.x + x)

    def draw_disassembly(self, focused):
        inner = self.inner
        self.ensure_visible()
        show_bytes = self.config.get("source.show-bytes")
        for y in range(inner.height):
            index = self.top + y
            if index >= len(self.instructions):
                break
            instruction = self.instructions[index]
            address = instruction["address"]
            gutter, marker = self.gutter(address, "", 0, False, focused)
            x = inner.put_segments(y, 0, gutter)
            cursor = index == self.cursor
            address_attr = self.color("address")
            if cursor:
                address_attr = self.color("selection" if focused else "selection-unfocused")
            segments = [("0x%x" % address, address_attr),
                        (" <+%d>: " % instruction["offset"], self.color("dim"))]
            if show_bytes:
                segments.append((instruction["bytes"].hex().ljust(18) + " ",
                                 self.color("dim")))
            segments.append((instruction["mnemonic"].ljust(8) + " ", self.color("keyword")))
            segments.extend((t, self.color(k))
                            for t, k in highlight.highlight_operands(instruction["operands"]))
            if instruction["comment"]:
                segments.append(("  ; " + instruction["comment"], self.color("comment")))
            full_attr = self.line_attr(marker)
            if full_attr is not None:
                segments = [(text, full_attr) for text, _ in segments]
                inner.fill(y, full_attr, x)
            text = "".join(t for t, _ in segments)
            segments = overlay(segments, self.search_ranges(text), self.color("search-match"))
            inner.put_segments(y, x, hscroll_segments(segments, self.hscroll))

    # Actions.

    def cursor_location(self):
        """Return ('line', path, line) or ('address', address) for the cursor."""
        if self.mode == "disassembly":
            if 0 <= self.cursor < len(self.instructions):
                return ("address", self.instructions[self.cursor]["address"])
            return None
        if self.file:
            return ("line", self.file.path, self.cursor + 1)
        return None

    def breakpoints_at_cursor(self):
        target = self.app.target
        location = self.cursor_location()
        result = []
        if not location or not target.IsValid():
            return result
        for breakpoint in target.breakpoint_iter():
            for bp_location in breakpoint:
                address = bp_location.GetAddress()
                if location[0] == "address":
                    match = bp_location.GetLoadAddress() == location[1]
                else:
                    line_entry = address.GetLineEntry()
                    match = (line_entry.IsValid() and line_entry.GetLine() == location[2]
                             and self.resolve_path(file_spec_path(line_entry.GetFileSpec()))
                             == location[1])
                if match:
                    result.append(breakpoint)
                    break
        return result

    def code_column(self, path, line, column):
        """Map the cursor column to a column of the line table: the first code
        that starts at or after the cursor, or the last one before it. Returns
        None if there is no column information for the line."""
        columns = set()
        file_spec = lldb.SBFileSpec(path, False)
        for module in self.app.target.module_iter():
            for compile_unit in module.compile_unit_iter():
                index = compile_unit.FindLineEntryIndex(0, line, file_spec, True)
                while index != 0xFFFFFFFF:
                    entry_column = compile_unit.GetLineEntryAtIndex(index).GetColumn()
                    if entry_column:
                        columns.add(entry_column)
                    index = compile_unit.FindLineEntryIndex(index + 1, line, file_spec, True)
        if not columns:
            return None
        after = [c for c in columns if c >= column]
        return min(after) if after else max(columns)

    def cursor_target(self):
        """Describe the position the cursor points to: (location, text) where
        location is ('address', address) or ('line', path, line, column) with
        column being None for the whole line."""
        location = self.cursor_location()
        if not location:
            return None, None
        if location[0] == "address":
            return location, "0x%x" % location[1]
        _, path, line = location
        column = self.code_column(path, line, self.source_column())
        where = "%s:%d" % (short_path(path), line)
        if column:
            where += ":%d" % column
        return ("line", path, line, column), where

    def create_breakpoint(self, one_shot=False, location=None, where=None):
        target = self.app.target
        if location is None:
            location = self.cursor_location()
            if not location:
                return None
        if location[0] == "address":
            breakpoint = target.BreakpointCreateByAddress(location[1])
            where = where or "0x%x" % location[1]
        elif len(location) > 3 and location[3]:
            breakpoint = target.BreakpointCreateByLocation(
                lldb.SBFileSpec(location[1], False), location[2], location[3], 0,
                lldb.SBFileSpecList())
        else:
            breakpoint = target.BreakpointCreateByLocation(location[1], location[2])
        where = where or "%s:%d" % (short_path(location[1]), location[2])
        if not breakpoint.IsValid():
            self.app.message("Unable to set a breakpoint at %s" % where, "console-error")
            return None
        if one_shot and breakpoint.GetNumLocations() == 0:
            target.BreakpointDelete(breakpoint.GetID())
            self.app.message("There is no code at %s" % where, "console-error")
            return None
        if one_shot:
            breakpoint.SetOneShot(True)
        else:
            self.app.message("Breakpoint %d: %s (%d location%s)" % (
                breakpoint.GetID(), where, breakpoint.GetNumLocations(),
                "" if breakpoint.GetNumLocations() == 1 else "s"))
        return breakpoint

    def handle_action(self, action, key, name):
        app = self.app
        page = max(1, (self.inner.height if self.inner else 2) - 1)
        if action == "up":
            self.move_cursor(self.cursor - 1)
        elif action == "down":
            self.move_cursor(self.cursor + 1)
        elif action == "page-up":
            self.move_cursor(self.cursor - page)
        elif action == "page-down":
            self.move_cursor(self.cursor + page)
        elif action == "top":
            self.move_cursor(0)
        elif action == "bottom":
            self.move_cursor(self.line_count() - 1)
        elif action == "expand":
            if self.mode == "source":
                self.set_column(self.column + 1)
            else:
                self.hscroll += 8
        elif action == "collapse":
            if self.mode == "source":
                self.set_column(self.column - 1)
            else:
                self.hscroll = max(0, self.hscroll - 8)
        elif action == "line-start":
            self.set_column(0)
            self.hscroll = 0
        elif action == "line-end":
            self.set_column(len(self.expanded_line(self.cursor)))
        elif action == "activate":
            self.confirm_run_to_cursor()
        elif action == "toggle-breakpoint":
            if not app.require_target():
                return True
            existing = self.breakpoints_at_cursor()
            if existing:
                for breakpoint in existing:
                    app.message("Deleted breakpoint %d" % breakpoint.GetID())
                    app.target.BreakpointDelete(breakpoint.GetID())
            else:
                self.create_breakpoint()
            app.invalidate()
        elif action == "toggle-breakpoint-enabled":
            existing = self.breakpoints_at_cursor()
            if not existing:
                app.message("There is no breakpoint on the selected line", "console-error")
            enable = not any(b.IsEnabled() for b in existing)
            for breakpoint in existing:
                breakpoint.SetEnabled(enable)
                app.message("%s breakpoint %d" % ("Enabled" if enable else "Disabled",
                                                  breakpoint.GetID()))
            app.invalidate()
        elif action == "run-to-line":
            self.run_to_cursor()
        elif action == "goto-pc":
            self.forced_mode = None
            self.location_key = None
            self.refresh()
        elif action == "goto-line":
            if self.mode == "source" and self.file:
                app.prompt("Go to line", "", self.goto_line)
        elif action == "open-file":
            self.open_file_picker()
        elif action == "toggle-disassembly":
            if not app.is_stopped():
                app.message("Disassembly is only available while the process is stopped",
                            "console-error")
                return True
            self.forced_mode = "source" if self.mode == "disassembly" else "disassembly"
            self.location_key = None
            self.refresh()
        elif action == "search":
            app.prompt("Search", self.search_text, self.search)
        elif action == "search-next":
            self.search(self.search_text, start=self.cursor + 1)
        else:
            return False
        return True

    def can_run_to_cursor(self):
        app = self.app
        if not app.require_target():
            return False
        if app.is_running():
            app.message("The process is running", "console-error")
            return False
        return self.cursor_location() is not None

    def confirm_run_to_cursor(self):
        if not self.can_run_to_cursor():
            return
        location, where = self.cursor_target()
        verb = "Continue" if self.app.is_stopped() else "Launch and run"
        self.app.confirm("Continue to here?", "%s to %s?" % (verb, where),
                         lambda: self.run_to_cursor(location, where))

    def run_to_cursor(self, location=None, where=None):
        """Run to the cursor with a one-shot breakpoint."""
        if not self.can_run_to_cursor():
            return
        if location is None:
            location, where = self.cursor_target()
        if self.create_breakpoint(one_shot=True, location=location, where=where):
            if self.app.is_stopped():
                self.app.run_command("process continue")
            else:
                self.app.run_command("process launch")

    def goto_line(self, text):
        try:
            line = int(text.strip())
        except ValueError:
            self.app.message("'%s' is not a line number" % text, "console-error")
            return
        self.move_cursor(line - 1)
        self.center_cursor()

    def line_text(self, index):
        if self.mode == "disassembly":
            instruction = self.instructions[index]
            return "%x %s %s %s" % (instruction["address"], instruction["mnemonic"],
                                    instruction["operands"], instruction["comment"])
        return self.file.lines[index]

    def search(self, text, start=None):
        self.search_text = text
        count = self.line_count()
        if not text or count == 0:
            return
        if start is None:
            start = self.cursor
        needle = text.lower()
        for offset in range(count):
            index = (start + offset) % count
            if needle in self.line_text(index).lower():
                self.move_cursor(index)
                if offset and index < start:
                    self.app.message("Search wrapped around", "dim")
                return
        self.app.message("'%s' not found" % text, "console-error")

    def open_file_picker(self):
        target = self.app.target
        paths = set()
        if target and target.IsValid():
            for module in target.module_iter():
                for compile_unit in module.compile_unit_iter():
                    path = file_spec_path(compile_unit.GetFileSpec())
                    if path:
                        paths.add(path)
        self.app.pick("Open source file (type to filter, or enter a path)",
                      sorted(paths), self.open_file, allow_custom=True)

    def open_file(self, path):
        path, _, line = path.partition(":") if re.match(r".*:\d+$", path) else (path, "", "")
        self.forced_mode = "source"
        if self.show_file(os.path.expanduser(path), int(line) if line else 1):
            self.app.focus("source")

    def handle_mouse(self, y, x, clicks):
        self.move_cursor(self.top + y)
        if x < 2:
            # The breakpoint/program counter markers.
            self.handle_action("toggle-breakpoint", None, None)
            return True
        if self.mode == "source" and x >= self.text_x:
            self.set_column(x - self.text_x + self.hscroll)
        if clicks >= 2:
            self.confirm_run_to_cursor()
        return True


# ----------------------------------------------------------------------------
# Variables
# ----------------------------------------------------------------------------


class VarNode(object):
    def __init__(self, path, value=None, label=None, kind="value", depth=0,
                 parent=None, loader=None, expression=None):
        self.path = path
        self.value = value
        self.label = label
        self.kind = kind  # value, group, more, message
        self.depth = depth
        self.parent = parent
        self.loader = loader
        self.expression = expression
        self.children = None

    def expandable(self):
        if self.kind == "group":
            return True
        if self.kind == "value" and self.value is not None and self.value.IsValid():
            return self.value.MightHaveChildren()
        return False


class VariablesPane(ListPane):
    name = "variables"
    title = "Variables"
    empty_text = "No variables"

    def __init__(self, app):
        ListPane.__init__(self, app)
        self.expanded = set()
        self.child_limits = {}
        self.roots = []
        self.values = {}
        self.previous_values = {}
        self.frame_key = None
        self.stop_id = None

    def refresh(self):
        frame = self.app.frame()
        selected_path = self.rows[self.selected].path if self.selected_row() else None
        if self.stop_id != self.app.stop_id:
            self.previous_values = self.values
            self.values = {}
            self.stop_id = self.app.stop_id
        self.roots = []
        if frame is None:
            self.empty_text = "Process is running" if self.app.is_running() else "No frame"
            self.rows = []
            return
        self.empty_text = "No variables"
        self.frame_key = "%s/%x" % (frame.GetFunctionName() or "", frame.GetCFA())

        dynamic = (lldb.eDynamicCanRunTarget if self.config.get("variables.dynamic-types")
                   else lldb.eNoDynamicValues)
        for index, expression in enumerate(self.app.expressions):
            options = lldb.SBExpressionOptions()
            options.SetFetchDynamicValue(dynamic)
            options.SetTimeoutInMicroSeconds(500000)
            value = frame.EvaluateExpression(expression, options)
            self.roots.append(VarNode("expr/%d/%s" % (index, expression), value,
                                      label=expression, expression=index))

        options = lldb.SBVariablesOptions()
        options.SetIncludeArguments(self.config.get("variables.show-arguments"))
        options.SetIncludeLocals(self.config.get("variables.show-locals"))
        options.SetIncludeStatics(self.config.get("variables.show-statics"))
        options.SetInScopeOnly(True)
        options.SetUseDynamic(dynamic)
        variables = frame.GetVariables(options)
        seen = set()
        for i in range(variables.GetSize()):
            value = variables.GetValueAtIndex(i)
            name = value.GetName() or "?"
            path = "var/" + name
            while path in seen:
                path += "'"
            seen.add(path)
            self.roots.append(VarNode(path, value))

        if self.config.get("variables.show-registers"):
            registers = frame.GetRegisters()

            def load_registers(node, registers=registers):
                return [VarNode(node.path + "/" + (r.GetName() or "?"), r,
                                depth=node.depth + 1, parent=node)
                        for r in registers]

            self.roots.append(VarNode("registers", label="Registers", kind="group",
                                      loader=load_registers))
        self.rebuild(selected_path)

    def children(self, node):
        if node.children is not None:
            return node.children
        if node.loader:
            node.children = node.loader(node)
            return node.children
        value = node.value
        limit = self.child_limits.get(node.path, self.config.get("variables.max-children"))
        count = value.GetNumChildren(limit + 1)
        children = []
        for i in range(min(count, limit)):
            child = value.GetChildAtIndex(i)
            name = child.GetName() or "[%d]" % i
            children.append(VarNode("%s/%s" % (node.path, name), child,
                                    depth=node.depth + 1, parent=node))
        if count > limit:
            children.append(VarNode(node.path + "/<more>", label="... more children",
                                    kind="more", depth=node.depth + 1, parent=node))
        node.children = children
        return children

    def rebuild(self, selected_path=None):
        rows = []

        def visit(node):
            rows.append(node)
            if node.path in self.expanded and node.expandable():
                for child in self.children(node):
                    visit(child)

        for root in self.roots:
            visit(root)
        self.rows = rows
        if selected_path is not None:
            for index, row in enumerate(rows):
                if row.path == selected_path:
                    self.select(index)
                    return
        self.select(self.selected)

    def row_segments(self, node, width):
        chars = self.app.chars
        indent = "  " * node.depth
        if node.expandable():
            marker = chars["expanded"] if node.path in self.expanded else chars["collapsed"]
        else:
            marker = " "
        segments = [(indent + marker + " ", self.color("dim"))]
        if node.kind == "group":
            segments.append((node.label, self.color("title")))
            return segments
        if node.kind in ("more", "message"):
            segments.append((node.label, self.color("dim")))
            return segments
        value = node.value
        if self.config.get("variables.show-types"):
            type_name = value.GetDisplayTypeName() or value.GetTypeName()
            if type_name:
                segments.append(("(%s) " % type_name, self.color("var-type")))
        name = node.label if node.label is not None else (value.GetName() or "")
        segments.append((name, self.color("keyword" if node.expression is not None
                                          else "var-name")))
        text, ok = value_text(value)
        key = (self.frame_key, node.path)
        self.values[key] = text
        changed = (self.config.get("variables.highlight-changes")
                   and key in self.previous_values and self.previous_values[key] != text)
        if text:
            segments.append((" = ", self.color("dim")))
            element = "var-value"
            if not ok:
                element = "error"
            elif changed:
                element = "var-changed"
            segments.append((text, self.color(element)))
        return segments

    def toggle(self, node, expand=None):
        if not node.expandable():
            return
        if expand is None:
            expand = node.path not in self.expanded
        if expand:
            self.expanded.add(node.path)
        else:
            self.expanded.discard(node.path)
        self.rebuild(node.path)

    def activate(self, node):
        if node.kind == "more":
            parent = node.parent
            limit = self.child_limits.get(parent.path, self.config.get("variables.max-children"))
            self.child_limits[parent.path] = limit * 2
            parent.children = None
            self.rebuild(node.path)
        else:
            self.toggle(node)

    def handle_action(self, action, key, name):
        node = self.selected_row()
        app = self.app
        if action == "expand" and node:
            if node.kind == "more":
                self.activate(node)
            elif node.path in self.expanded and node.expandable():
                self.select(self.selected + 1)
            else:
                self.toggle(node, True)
        elif action == "collapse" and node:
            if node.path in self.expanded:
                self.toggle(node, False)
            elif node.parent is not None:
                self.rebuild(node.parent.path)
        elif action == "add":
            if app.is_stopped():
                app.prompt("Watch expression", "", self.add_expression)
            else:
                app.message("The process must be stopped to evaluate expressions",
                            "console-error")
        elif action == "delete" and node and node.expression is not None:
            del app.expressions[node.expression]
            self.refresh()
        elif action == "edit" and node and node.kind == "value":
            text, _ = value_text(node.value)
            app.prompt("New value for %s" % (node.label or node.value.GetName()),
                       node.value.GetValue() or "",
                       lambda new_value, node=node: self.set_value(node, new_value))
        elif action == "watch" and node and node.kind == "value":
            error = lldb.SBError()
            watchpoint = node.value.Watch(True, False, True, error)
            if error.Fail() or not watchpoint.IsValid():
                app.message("Unable to watch %s: %s" % (node.value.GetName(),
                                                        error.GetCString()), "console-error")
            else:
                app.message("Watchpoint %d: %s" % (watchpoint.GetID(), node.value.GetName()))
            app.invalidate()
        elif action == "print" and node and node.kind == "value":
            stream = lldb.SBStream()
            node.value.GetDescription(stream)
            app.console.append_text(stream.GetData())
        else:
            return ListPane.handle_action(self, action, key, name)
        return True

    def add_expression(self, text):
        if text.strip():
            self.app.expressions.append(text.strip())
            self.refresh()

    def set_value(self, node, text):
        error = lldb.SBError()
        if not node.value.SetValueFromCString(text, error):
            self.app.message("Unable to change the value: %s" % (
                error.GetCString() or "unknown error"), "console-error")
        self.app.invalidate()


# ----------------------------------------------------------------------------
# Threads and backtrace
# ----------------------------------------------------------------------------


class ThreadsPane(ListPane):
    name = "threads"
    title = "Threads"
    empty_text = "No process"

    def refresh(self):
        self.rows = []
        if not self.app.is_stopped():
            self.empty_text = "Process is running" if self.app.is_running() else "No process"
            return
        process = self.app.process
        selected = process.GetSelectedThread().GetIndexID()
        for index, thread in enumerate(process):
            self.rows.append(thread)
            if thread.GetIndexID() == selected:
                self.selected = index
        self.select(self.selected)

    def title_text(self):
        return "Threads (%d)" % len(self.rows) if self.rows else "Threads"

    def row_segments(self, thread, width):
        selected = self.app.process.GetSelectedThread().GetIndexID() == thread.GetIndexID()
        segments = [((self.app.chars["pc"] if selected else " ") + " ",
                     self.color("pc-marker")),
                    ("#%d " % thread.GetIndexID(), self.color("number")),
                    ("tid 0x%x" % thread.GetThreadID(), self.color("address"))]
        name = thread.GetName()
        if name:
            segments.append((" '%s'" % name, self.color("var-name")))
        queue = thread.GetQueueName() if self.config.get("threads.show-queues") else None
        if queue:
            segments.append((" queue '%s'" % queue, self.color("dim")))
        if self.config.get("threads.show-frame"):
            frame = thread.GetFrameAtIndex(0)
            if frame.IsValid():
                segments.append((" " + (frame.GetDisplayFunctionName() or "0x%x" % frame.GetPC()),
                                 self.color("function")))
        if thread.GetStopReason() not in (lldb.eStopReasonNone, lldb.eStopReasonInvalid):
            segments.append((" " + (thread.GetStopDescription(256) or ""),
                             self.color("stop-reason")))
        return segments

    def activate(self, thread):
        self.app.process.SetSelectedThread(thread)
        self.app.invalidate()


class BacktracePane(ListPane):
    name = "backtrace"
    title = "Backtrace"
    empty_text = "No process"

    def __init__(self, app):
        ListPane.__init__(self, app)
        self.thread_id = None

    def refresh(self):
        self.rows = []
        thread = self.app.thread()
        if thread is None:
            self.empty_text = "Process is running" if self.app.is_running() else "No process"
            return
        selected = thread.GetSelectedFrame().GetFrameID()
        show_hidden = self.config.get("backtrace.show-hidden")
        limit = self.config.get("backtrace.max-frames")
        for frame in thread:
            if len(self.rows) >= limit:
                break
            if (not show_hidden and frame.GetFrameID() != selected
                    and hasattr(frame, "IsHidden") and frame.IsHidden()):
                continue
            self.rows.append(frame)
            if frame.GetFrameID() == selected:
                self.selected = len(self.rows) - 1
        self.thread_id = thread.GetIndexID()
        self.select(self.selected)

    def title_text(self):
        if self.thread_id is not None and self.rows:
            return "Backtrace (thread #%d)" % self.thread_id
        return "Backtrace"

    def row_segments(self, frame, width):
        thread = frame.GetThread()
        selected = thread.GetSelectedFrame().GetFrameID() == frame.GetFrameID()
        segments = [((self.app.chars["pc"] if selected else " ") + " ",
                     self.color("pc-marker")),
                    ("#%-2d " % frame.GetFrameID(), self.color("number"))]
        if self.config.get("backtrace.show-addresses"):
            segments.append(("0x%x " % frame.GetPC(), self.color("address")))
        if self.config.get("backtrace.show-modules"):
            module = frame.GetModule()
            if module.IsValid() and module.GetFileSpec().GetFilename():
                segments.append((module.GetFileSpec().GetFilename() + "`", self.color("dim")))
        name = frame.GetDisplayFunctionName() or frame.GetFunctionName() or "0x%x" % frame.GetPC()
        segments.append((name, self.color("function")))
        location = describe_line_entry(frame.GetLineEntry())
        if location:
            segments.append((" at " + location, self.color("dim")))
        if frame.IsInlined():
            segments.append((" [inlined]", self.color("dim")))
        return segments

    def activate(self, frame):
        thread = frame.GetThread()
        thread.SetSelectedFrame(frame.GetFrameID())
        self.app.process.SetSelectedThread(thread)
        self.app.invalidate()


# ----------------------------------------------------------------------------
# Breakpoints and watchpoints
# ----------------------------------------------------------------------------


class BreakpointsPane(ListPane):
    name = "breakpoints"
    title = "Breakpoints"
    empty_text = "No breakpoints (press 'a' to add one)"

    def __init__(self, app):
        ListPane.__init__(self, app)
        self.expanded = set()

    def refresh(self):
        selected = self.selected_row()
        selected_key = selected[:2] if selected else None
        self.rows = []
        target = self.app.target
        if not target or not target.IsValid():
            self.empty_text = "No target"
            return
        self.empty_text = "No breakpoints (press 'a' to add one)"
        for breakpoint in target.breakpoint_iter():
            self.rows.append((breakpoint.GetID(), None, breakpoint, None))
            if breakpoint.GetID() in self.expanded:
                for location in breakpoint:
                    self.rows.append((breakpoint.GetID(), location.GetID(), breakpoint,
                                      location))
        for index, row in enumerate(self.rows):
            if row[:2] == selected_key:
                self.selected = index
        self.select(self.selected)

    def location_text(self, location):
        address = location.GetAddress()
        line_entry = address.GetLineEntry()
        function = address.GetFunction()
        symbol = address.GetSymbol()
        name = None
        if function.IsValid():
            name = function.GetDisplayName() or function.GetName()
        elif symbol.IsValid():
            name = symbol.GetDisplayName() or symbol.GetName()
        where = describe_line_entry(line_entry)
        parts = []
        if where:
            parts.append(where)
        if name:
            parts.append(name)
        if not parts:
            parts.append("0x%x" % location.GetLoadAddress())
        return " ".join(parts)

    def row_segments(self, row, width):
        bp_id, location_id, breakpoint, location = row
        chars = self.app.chars
        if location is not None:
            enabled = location.IsEnabled() and breakpoint.IsEnabled()
            segments = [("    ", 0),
                        (chars["breakpoint" if enabled else "breakpoint-disabled"] + " ",
                         self.color("breakpoint" if enabled else "breakpoint-disabled")),
                        ("%d.%d " % (bp_id, location_id), self.color("number")),
                        (self.location_text(location), self.color("text"))]
            if not location.IsResolved():
                segments.append((" (unresolved)", self.color("dim")))
            return segments
        enabled = breakpoint.IsEnabled()
        count = breakpoint.GetNumLocations()
        if count > 1:
            marker = chars["expanded"] if bp_id in self.expanded else chars["collapsed"]
        else:
            marker = " "
        segments = [(marker + " ", self.color("dim")),
                    (chars["breakpoint" if enabled else "breakpoint-disabled"] + " ",
                     self.color("breakpoint" if enabled else "breakpoint-disabled")),
                    ("%d " % bp_id, self.color("number"))]
        if count == 0:
            stream = lldb.SBStream()
            breakpoint.GetDescription(stream, False)
            description = stream.GetData().strip()
            description = re.sub(r"^\d+:\s*", "", description)
            description = re.sub(r",\s*locations\s*=\s*0.*$", "", description)
            segments.append((description, self.color("text")))
            segments.append((" (pending)", self.color("dim")))
        elif count == 1:
            segments.append((self.location_text(breakpoint.GetLocationAtIndex(0)),
                             self.color("text")))
        else:
            segments.append(("%d locations" % count, self.color("text")))
        names = lldb.SBStringList()
        breakpoint.GetNames(names)
        if names.GetSize():
            segments.append((" [%s]" % ", ".join(names.GetStringAtIndex(i)
                                                  for i in range(names.GetSize())),
                             self.color("var-name")))
        hits = breakpoint.GetHitCount()
        if hits:
            segments.append((" hits:%d" % hits, self.color("stop-reason")))
        condition = breakpoint.GetCondition()
        if condition:
            segments.append((" if " + condition, self.color("keyword")))
        if breakpoint.IsOneShot():
            segments.append((" (one-shot)", self.color("dim")))
        return segments

    def activate(self, row):
        bp_id, location_id, breakpoint, location = row
        if location is None and breakpoint.GetNumLocations() > 1:
            if bp_id in self.expanded:
                self.expanded.discard(bp_id)
            else:
                self.expanded.add(bp_id)
            self.refresh()
        self.show_location(row, report=True)

    def show_location(self, row, report=False):
        """Show the location of a breakpoint in the source panel. The focus
        stays in this panel."""
        if row is None:
            return
        bp_id, location_id, breakpoint, location = row
        if location is None:
            if breakpoint.GetNumLocations() == 0:
                return
            location = breakpoint.GetLocationAtIndex(0)
        line_entry = location.GetAddress().GetLineEntry()
        path = file_spec_path(line_entry.GetFileSpec()) if line_entry.IsValid() else None
        source = self.app.source
        if path and line_entry.GetLine() and source.load_file(path):
            if source.mode == "disassembly":
                source.forced_mode = "source"
                if source.location_key:
                    # Don't treat this as a new stop location on the next
                    # refresh (which would show the program counter again).
                    source.location_key = source.location_key[:3] + ("source",)
            source.show_file(path, line_entry.GetLine())
            if line_entry.GetColumn():
                source.set_column(line_entry.GetColumn() - 1)
        elif report:
            self.app.message("Breakpoint location %d.%d has no source line" % (
                bp_id, location.GetID()), "console-error")

    def handle_mouse(self, y, x, clicks):
        handled = ListPane.handle_mouse(self, y, x, clicks)
        if handled and clicks < 2:
            self.show_location(self.selected_row())
        return handled

    def handle_action(self, action, key, name):
        row = self.selected_row()
        app = self.app
        if action == "toggle" and row:
            bp_id, location_id, breakpoint, location = row
            if location is not None:
                location.SetEnabled(not location.IsEnabled())
            else:
                breakpoint.SetEnabled(not breakpoint.IsEnabled())
            app.invalidate()
        elif action == "delete" and row:
            app.target.BreakpointDelete(row[0])
            app.message("Deleted breakpoint %d" % row[0])
            app.invalidate()
        elif action == "edit" and row:
            breakpoint = row[2]
            app.prompt("Condition for breakpoint %d (empty to remove)" % row[0],
                       breakpoint.GetCondition() or "",
                       lambda text, b=breakpoint: (b.SetCondition(text.strip() or None),
                                                   app.invalidate()))
        elif action == "add":
            if app.require_target():
                app.prompt("Breakpoint location (e.g. main, file.c:12, 0x1234)", "",
                           lambda text: text.strip() and app.run_command(
                               "_regexp-break " + text.strip()))
        elif action in ("expand", "collapse") and row and row[3] is None:
            if (action == "expand") != (row[0] in self.expanded):
                self.activate(row)
        elif action in ("up", "down", "page-up", "page-down", "top", "bottom"):
            ListPane.handle_action(self, action, key, name)
            self.show_location(self.selected_row())
        else:
            return ListPane.handle_action(self, action, key, name)
        return True


class WatchpointsPane(ListPane):
    name = "watchpoints"
    title = "Watchpoints"
    empty_text = "No watchpoints (press 'a' to add one)"

    def refresh(self):
        self.rows = []
        target = self.app.target
        if not target or not target.IsValid():
            self.empty_text = "No target"
            return
        self.empty_text = "No watchpoints (press 'a' to add one)"
        self.rows = list(target.watchpoint_iter())
        self.select(self.selected)

    def row_segments(self, watchpoint, width):
        chars = self.app.chars
        enabled = watchpoint.IsEnabled()
        kind = ("r" if watchpoint.IsWatchingReads() else "") + (
            "w" if watchpoint.IsWatchingWrites() else "")
        segments = [(chars["breakpoint" if enabled else "breakpoint-disabled"] + " ",
                     self.color("breakpoint" if enabled else "breakpoint-disabled")),
                    ("%d " % watchpoint.GetID(), self.color("number"))]
        spec = watchpoint.GetWatchSpec() if hasattr(watchpoint, "GetWatchSpec") else None
        if spec:
            segments.append((spec + " ", self.color("var-name")))
        segments.append(("0x%x" % watchpoint.GetWatchAddress(), self.color("address")))
        segments.append((" size:%d %s" % (watchpoint.GetWatchSize(), kind), self.color("dim")))
        hits = watchpoint.GetHitCount()
        if hits:
            segments.append((" hits:%d" % hits, self.color("stop-reason")))
        condition = watchpoint.GetCondition()
        if condition:
            segments.append((" if " + condition, self.color("keyword")))
        return segments

    def handle_action(self, action, key, name):
        watchpoint = self.selected_row()
        app = self.app
        if action == "toggle" and watchpoint:
            watchpoint.SetEnabled(not watchpoint.IsEnabled())
            app.invalidate()
        elif action == "delete" and watchpoint:
            app.message("Deleted watchpoint %d" % watchpoint.GetID())
            app.target.DeleteWatchpoint(watchpoint.GetID())
            app.invalidate()
        elif action == "edit" and watchpoint:
            app.prompt("Condition for watchpoint %d (empty to remove)" % watchpoint.GetID(),
                       watchpoint.GetCondition() or "",
                       lambda text, w=watchpoint: (w.SetCondition(text.strip() or None),
                                                   app.invalidate()))
        elif action == "add":
            if app.is_stopped():
                app.prompt("Watch variable or expression (write access)", "",
                           self.add_watchpoint)
            else:
                app.message("The process must be stopped to set a watchpoint",
                            "console-error")
        else:
            return ListPane.handle_action(self, action, key, name)
        return True

    def add_watchpoint(self, text):
        text = text.strip()
        if not text:
            return
        if re.match(r"^[A-Za-z_][\w.\[\]>-]*$", text):
            self.app.run_command("watchpoint set variable " + text)
        else:
            self.app.run_command("watchpoint set expression -- " + text)


# ----------------------------------------------------------------------------
# Console
# ----------------------------------------------------------------------------

_SGR_COLORS = ["black", "red", "green", "yellow", "blue", "magenta", "cyan", "white"]
_ESCAPE = re.compile(r"\x1b\[([0-9;?]*)([A-Za-z])|\x1b[()][A-Za-z0-9]|\x1b[=>78]|\x1b\][^\x07]*\x07")


class ConsolePane(Pane):
    name = "console"
    title = "Console"

    def __init__(self, app):
        Pane.__init__(self, app)
        # Each line is a list of (text, style) where style is the name of a
        # color element or a color specification string prefixed with '='.
        self.lines = []
        self.partial = {}
        self.scroll_offset = 0
        self.history = []
        self.input = TextInput(history=self.history, completer=self.complete)
        self.stdin_mode = False
        self.completions = None

    # Output.

    def parse_ansi(self, text, element, state):
        """Split text with ANSI escape sequences into styled segments. state is
        the current SGR state [fg, bg, bold, underline] and is updated."""
        segments = []
        position = 0
        for match in _ESCAPE.finditer(text):
            if match.start() > position:
                segments.append((text[position : match.start()], self.style(element, state)))
            position = match.end()
            if match.group(2) != "m":
                continue
            codes = [c for c in (match.group(1) or "0").split(";")]
            i = 0
            while i < len(codes):
                code = int(codes[i]) if codes[i].isdigit() else 0
                if code == 0:
                    state[:] = [None, None, False, False]
                elif code == 1:
                    state[2] = True
                elif code == 22:
                    state[2] = False
                elif code == 4:
                    state[3] = True
                elif code == 24:
                    state[3] = False
                elif 30 <= code <= 37:
                    state[0] = _SGR_COLORS[code - 30]
                elif 90 <= code <= 97:
                    state[0] = "bright-" + _SGR_COLORS[code - 90]
                elif 40 <= code <= 47:
                    state[1] = _SGR_COLORS[code - 40]
                elif code in (38, 48) and i + 2 < len(codes) and codes[i + 1] == "5":
                    state[0 if code == 38 else 1] = codes[i + 2]
                    i += 2
                elif code == 39:
                    state[0] = None
                elif code == 49:
                    state[1] = None
                i += 1
        if position < len(text):
            segments.append((text[position:], self.style(element, state)))
        return segments

    @staticmethod
    def style(element, state):
        if not any(state):
            return element
        parts = [state[0] or "default", state[1] or "default"]
        if state[2]:
            parts.append("bold")
        if state[3]:
            parts.append("underline")
        return "=" + ",".join(parts)

    def append_text(self, text, element="text", stream=None):
        """Append text to the console. If stream is given, text that doesn't
        end in a newline is shown as partial line that is continued by the
        next call for the same stream."""
        if not text:
            return
        previous, state = self.partial.pop(stream, ([], [None, None, False, False])) \
            if stream else ([], [None, None, False, False])
        if previous and self.lines and self.lines[-1][1] == stream:
            self.lines.pop()
        pieces = text.replace("\r\n", "\n").split("\n")
        for i, piece in enumerate(pieces):
            if "\r" in piece:
                piece = piece.rsplit("\r", 1)[-1]
                previous = []
            segments = self.expand_tabs(previous + self.parse_ansi(piece, element, state))
            previous = []
            if i < len(pieces) - 1:
                self.add_line(segments)
            elif segments:
                if stream:
                    self.partial[stream] = (segments, state)
                self.add_line(segments, partial_key=stream)
        self.app.dirty = True

    @staticmethod
    def expand_tabs(segments):
        if not any("\t" in text for text, _ in segments):
            return segments
        result = []
        column = 0
        for text, style in segments:
            if "\t" in text:
                pieces = []
                for char in text:
                    if char == "\t":
                        width = 8 - column % 8
                        pieces.append(" " * width)
                        column += width
                    else:
                        pieces.append(char)
                        column += 1
                text = "".join(pieces)
            else:
                column += len(text)
            result.append((text, style))
        return result

    def add_line(self, segments, partial_key=None):
        if self.lines and self.lines[-1][1] is not None:
            # The partial line of a stream is followed by other output: keep
            # it as it is and continue the stream on a new line.
            self.partial.pop(self.lines[-1][1], None)
            self.lines[-1] = (self.lines[-1][0], None)
        self.lines.append((segments, partial_key))
        limit = self.config.get("console.scrollback")
        if len(self.lines) > limit:
            del self.lines[: len(self.lines) - limit]
        if self.scroll_offset:
            self.scroll_offset += 1

    def append_command(self, prompt, command):
        self.add_line([(prompt, "prompt"), (command, "console-command")])
        self.app.dirty = True

    # Drawing.

    def title_text(self):
        title = "Console"
        if self.app.focused == "console" and not self.stdin_mode:
            title += " [Tab: complete/next panel, Esc: leave]"
        if self.stdin_mode:
            title += " [process input mode: C-t to switch back]"
        if self.scroll_offset:
            title += " [scrolled: %d]" % self.scroll_offset
        return title

    def resolve_style(self, style):
        if style.startswith("="):
            return self.app.colors.spec(style[1:])
        return self.color(style)

    def wrapped(self, segments, width):
        """Split a line into rows of at most width columns."""
        rows = [[]]
        used = 0
        for text, style in segments:
            while text:
                room = width - used
                if room <= 0:
                    rows.append([])
                    used = 0
                    room = width
                piece = clip(text, room)
                if not piece:
                    piece = text[0]
                rows[-1].append((piece, style))
                used += text_width(piece)
                text = text[len(piece) :]
        return rows

    def draw(self, focused):
        self.draw_frame(focused)
        inner = self.inner
        if inner.height <= 0 or inner.width <= 0:
            return
        output_height = inner.height - 1
        rows = []
        needed = output_height + self.scroll_offset
        for segments, _ in reversed(self.lines):
            line_rows = self.wrapped(segments, inner.width)
            rows[:0] = line_rows
            if len(rows) >= needed:
                break
        max_offset = max(0, len(rows) - output_height) if len(rows) < needed else self.scroll_offset
        self.scroll_offset = min(self.scroll_offset, max_offset)
        end = len(rows) - self.scroll_offset
        visible = rows[max(0, end - output_height) : end]
        start_y = output_height - len(visible)
        for i, row in enumerate(visible):
            inner.put_segments(start_y + i, 0,
                               [(text, self.resolve_style(style)) for text, style in row])
        if self.completions:
            line = "  ".join(self.completions)
            inner.put(output_height - 1, 0, " " * inner.width, self.color("dialog"))
            inner.put(output_height - 1, 0, line, self.color("dim"))
        prompt = "(stdin) " if self.stdin_mode else self.app.prompt_text()
        self.cursor = self.input.draw(inner, inner.height - 1, 0, inner.width,
                                      self.color("text"), prompt, self.color("prompt"),
                                      focused)

    def cursor_position(self):
        return getattr(self, "cursor", None)

    # Input.

    def complete(self, text_input):
        if self.stdin_mode:
            return
        matches = lldb.SBStringList()
        line = text_input.text
        cursor = text_input.cursor
        count = self.app.debugger.GetCommandInterpreter().HandleCompletion(
            line, cursor, 0, -1, matches)
        if count <= 0 and not matches.GetSize():
            self.completions = None
            return
        common = matches.GetStringAtIndex(0) if matches.GetSize() else ""
        candidates = [matches.GetStringAtIndex(i) for i in range(1, matches.GetSize())]
        if common:
            text_input.text = line[:cursor] + common + line[cursor:]
            text_input.cursor = cursor + len(common)
        if len(candidates) == 1 and not common:
            # Only one candidate: complete the word.
            word_start = max(line.rfind(" ", 0, cursor) + 1, 0)
            text_input.text = line[:word_start] + candidates[0] + line[cursor:]
            text_input.cursor = word_start + len(candidates[0])
            candidates = []
        self.completions = candidates[:50] if len(candidates) > 1 else None

    def submit(self):
        command = self.input.text
        self.input.set_text("")
        self.scroll_offset = 0
        if self.stdin_mode:
            self.app.send_process_input(command + "\n")
            return
        if command.strip():
            if not self.history or self.history[-1] != command:
                self.history.append(command)
                limit = self.config.get("console.history-size")
                if len(self.history) > limit:
                    del self.history[: len(self.history) - limit]
        elif self.config.get("console.repeat-on-empty") and self.history:
            command = self.history[-1]
        else:
            self.append_command(self.app.prompt_text(), "")
            return
        self.app.run_command(command)

    def handle_key(self, key, name):
        if name != "Tab":
            self.completions = None
        if name == "Enter":
            self.submit()
            return True
        if name == "PgUp":
            self.scroll(max(1, self.inner.height - 2))
            return True
        if name == "PgDn":
            self.scroll(-max(1, self.inner.height - 2))
            return True
        if name == "C-t":
            self.stdin_mode = not self.stdin_mode
            return True
        if name == "Esc":
            if self.input.text:
                self.input.set_text("")
            else:
                self.app.focus_previous()
            return True
        return self.input.handle_key(key, name)

    def handle_action(self, action, key, name):
        if action == "search":
            self.app.prompt("Search console", "", self.search)
            return True
        return False

    def search(self, text):
        if not text:
            return
        needle = text.lower()
        total = len(self.lines)
        for distance in range(self.scroll_offset + 1, total):
            segments, _ = self.lines[total - 1 - distance]
            if needle in "".join(t for t, _ in segments).lower():
                self.scroll_offset = distance
                return
        self.app.message("'%s' not found" % text, "console-error")

    def scroll(self, lines):
        self.scroll_offset = max(0, self.scroll_offset + lines)
        return True
