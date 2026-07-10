# -*- coding: utf-8 -*-

# pythonconsole.py -- Console widget
#
# Copyright (C) 2006 - Steve Frécinaux
# Copyright (C) 2012-2021 MATE Developers
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.

# Parts from "Interactive Python-GTK Console" (stolen from epiphany's console.py)
#     Copyright (C), 1998 James Henstridge <james@daa.com.au>
#     Copyright (C), 2005 Adam Hooper <adamh@densi.com>
# Bits from pluma Python Console Plugin
#     Copyrignt (C), 2005 Raphaël Slinckx

import os
import platform
import sys
import re
import traceback
from gi.repository import GObject, Gdk, Gtk, Pango

from .config import PythonConsoleConfig

__all__ = ('PythonConsole', 'OutFile')

# Commands recognized by the terminal regardless of the current mode. They
# take precedence over language evaluation, so e.g. typing the bare word
# "list" always shows the buffer instead of evaluating the "list" builtin.
_BARE_COMMANDS = ('help', 'clear', 'list', 'new', 'run', 'edit', 'repl', 'mode', 'save', 'load', 'quit')
_ARG_COMMAND_RE = re.compile(r'^(save|load)\s+(\S+)$')

HELP_TEXT = (
    "Available commands:\n"
    "  help          show this list of commands\n"
    "  clear         clear the screen (the code buffer is kept)\n"
    "  list          show the code currently stored in the buffer\n"
    "  new           clear the code buffer\n"
    "  run           run all the code stored in the buffer\n"
    "  edit          switch to buffer edit mode\n"
    "  repl          switch to immediate execution mode\n"
    "  mode          show the current mode\n"
    "  save <file>   save the buffer to a file\n"
    "  load <file>   load a file into the buffer\n"
    "  quit          close the console\n"
)

class PythonConsole(Gtk.ScrolledWindow):

    __gsignals__ = {
        'grab-focus' : 'override',
    }

    DEFAULT_FONT = "Monospace 10"

    def __init__(self, namespace = {}):
        Gtk.ScrolledWindow.__init__(self)

        self.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
        self.set_shadow_type(Gtk.ShadowType.IN)
        self.view = Gtk.TextView()
        self.view.set_editable(True)
        self.view.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        self.add(self.view)
        self.view.show()

        buffer = self.view.get_buffer()
        self.normal = buffer.create_tag("normal")
        self.error  = buffer.create_tag("error")
        self.command = buffer.create_tag("command")

        self.config = PythonConsoleConfig()
        self.config.add_handler(self.apply_preferences)
        self.apply_preferences()

        self.__spaces_pattern = re.compile(r'^\s+')
        self.namespace = namespace
        self._window = namespace.get('window') if namespace else None

        # Set up hooks for standard output.
        self.stdout = OutFile(self, sys.stdout.fileno(), self.normal)
        self.stderr = OutFile(self, sys.stderr.fileno(), self.error)

        self.__reset()

        # Signals
        self.view.connect("key-press-event", self.__key_press_event_cb)
        buffer.connect("mark-set", self.__mark_set_cb)

    def __reset(self):
        """Put the console back to its just-opened state: repl mode, an
        empty code buffer, no history, and a fresh version/help banner.
        Used both at construction and after "quit" actually closes the
        console, so reopening it starts a clean session rather than
        resuming wherever the last one left off."""
        self.block_command = False

        # Terminal mode: 'repl' runs each line/block as soon as it is
        # entered; 'edit' stores everything typed into self.code_buffer
        # as plain text instead, for later "run"/"save".
        self.mode = 'repl'
        self.code_buffer = ''
        # Code that "clear" hid from the screen but that's still part of
        # the buffer; see __sync_code_buffer/__cmd_clear.
        self.__hidden_prefix = ''
        if hasattr(self, '_PythonConsole__output_ranges'):
            self.__clear_output_ranges()
        self.__output_ranges = []
        self.__output_range_counter = 0
        self.__pending_run_code = None

        self.history = ['']
        self.history_pos = 0
        self.current_command = ''
        if self.namespace is not None:
            self.namespace['__history__'] = self.history

        buffer = self.view.get_buffer()
        buffer.set_text('')

        # Startup banner: interpreter version, then how to get help.
        version_line = "Python %s\n" % platform.python_version()
        help_line = "Type 'help' to list available commands and descriptions.\n"
        buffer.insert(buffer.get_end_iter(), version_line + help_line)

        end = buffer.get_end_iter()
        mark = buffer.get_mark("input-line")
        if mark is None:
            buffer.create_mark("input-line", end, True)
        else:
            buffer.move_mark(mark, end)

        buffer.insert(buffer.get_end_iter(), ">>> ")
        end = buffer.get_end_iter()
        mark = buffer.get_mark("input")
        if mark is None:
            buffer.create_mark("input", end, True)
        else:
            buffer.move_mark(mark, end)

        self.view.set_editable(True)

    def do_grab_focus(self):
        self.view.grab_focus()
        # Reopening the console (e.g. after "quit" hid the bottom panel)
        # must not land on a stale cursor position: if the caret was left
        # somewhere the repl mode considers read-only, set_editable()
        # would otherwise stick at False with no further mark-set event
        # to correct it, making the console look frozen.
        buffer = self.view.get_buffer()
        buffer.place_cursor(buffer.get_end_iter())
        self.view.set_editable(True)

    def apply_preferences(self, *args):
        self.error.set_property("foreground", self.config.color_error)
        self.command.set_property("foreground", self.config.color_command)

        if self.config.use_system_font:
            font_name = self.config.monospace_font_name
        else:
            font_name = self.config.font

        font_desc = None
        try:
            font_desc = Pango.FontDescription(font_name)
        except:
            try:
                font_desc = Pango.FontDescription(self.config.monospace_font_name)
            except:
                try:
                    font_desc = Pango.FontDescription(self.DEFAULT_FONT)
                except:
                    pass

        if font_desc:
            self.view.modify_font(font_desc)

    def stop(self):
        self.namespace = None

    def __match_command(self, stripped):
        """Return (name, arg) if stripped is a reserved terminal command."""
        if stripped in _BARE_COMMANDS:
            return (stripped, None)
        m = _ARG_COMMAND_RE.match(stripped)
        if m:
            return (m.group(1), m.group(2))
        return None

    def __key_press_event_cb(self, view, event):
        modifier_mask = Gtk.accelerator_get_default_mod_mask()
        event_state = event.state & modifier_mask
        keyname = Gdk.keyval_name(event.keyval)
        buffer = view.get_buffer()

        if keyname == "d" and event_state == Gdk.ModifierType.CONTROL_MASK:
            self.destroy()
            return True

        if self.mode == 'edit':
            return self.__edit_key_press_event_cb(view, buffer, keyname, event_state)

        return self.__repl_key_press_event_cb(view, buffer, keyname, event_state)

    def __edit_key_press_event_cb(self, view, buffer, keyname, event_state):
        if keyname == "Return":
            ins = buffer.get_iter_at_mark(buffer.get_insert())
            line_no = ins.get_line()
            line_start = buffer.get_iter_at_line(line_no)

            if line_no + 1 < buffer.get_line_count():
                delete_end = buffer.get_iter_at_line(line_no + 1)
                line_end = buffer.get_iter_at_line(line_no + 1)
                line_end.backward_char()
            else:
                line_end = buffer.get_end_iter()
                delete_end = line_end

            line = buffer.get_text(line_start, line_end, False)
            cmd = self.__match_command(line.strip())
            if cmd:
                command_offset = line_start.get_offset()
                buffer.delete(line_start, delete_end)
                name, arg = cmd
                if name == 'run':
                    self.__pending_run_code = self.__collect_code_buffer(
                        buffer.get_iter_at_offset(command_offset))
                self.dispatch_command(name, arg)
            else:
                if ins.compare(buffer.get_end_iter()) != 0:
                    # Editing an earlier line: let GTK split it normally.
                    return False
                buffer.insert(buffer.get_end_iter(), "\n")
                buffer.place_cursor(buffer.get_end_iter())
                GObject.idle_add(self.scroll_to_end)
            return True

        # Everything else (navigation, backspace, home/end, up/down) uses
        # plain GtkTextView behaviour so the whole buffer stays editable.
        return False

    def __repl_key_press_event_cb(self, view, buffer, keyname, event_state):
        if keyname == "Return" and \
             event_state == Gdk.ModifierType.CONTROL_MASK:
            # Get the command
            inp_mark = buffer.get_mark("input")
            inp = buffer.get_iter_at_mark(inp_mark)
            cur = buffer.get_end_iter()
            line = buffer.get_text(inp, cur, False)
            self.current_command = self.current_command + line + "\n"
            self.history_add(line)

            # Prepare the new line
            cur = buffer.get_end_iter()
            buffer.insert(cur, "\n... ")
            cur = buffer.get_end_iter()
            buffer.move_mark(inp_mark, cur)

            # Keep indentation of precendent line
            spaces = re.match(self.__spaces_pattern, line)
            if spaces is not None:
                buffer.insert(cur, line[spaces.start() : spaces.end()])
                cur = buffer.get_end_iter()

            buffer.place_cursor(cur)
            GObject.idle_add(self.scroll_to_end)
            return True

        elif keyname == "Return":
            # Get the marks
            lin_mark = buffer.get_mark("input-line")
            inp_mark = buffer.get_mark("input")

            # Get the command line
            inp = buffer.get_iter_at_mark(inp_mark)
            cur = buffer.get_end_iter()
            line = buffer.get_text(inp, cur, False)

            # Make the line blue
            lin = buffer.get_iter_at_mark(lin_mark)
            buffer.apply_tag(self.command, lin, cur)
            buffer.insert(cur, "\n")

            cmd = None if self.block_command else self.__match_command(line.strip())
            if cmd:
                self.history_add(line)
                name, arg = cmd
                self.dispatch_command(name, arg)
                GObject.idle_add(self.scroll_to_end)
                return True

            self.current_command = self.current_command + line + "\n"
            self.history_add(line)

            cur_strip = self.current_command.rstrip()

            if cur_strip.endswith(":") \
            or (self.current_command[-2:] != "\n\n" and self.block_command):
                # Unfinished block command
                self.block_command = True
                com_mark = "... "
            elif cur_strip.endswith("\\"):
                com_mark = "... "
            else:
                # Eval the command
                self.__run(self.current_command)
                self.__append_code_buffer(self.current_command)
                self.current_command = ''
                self.block_command = False
                com_mark = ">>> "

            self.__show_repl_prompt(com_mark)
            GObject.idle_add(self.scroll_to_end)
            return True

        elif keyname == "KP_Down" or keyname == "Down":
            # Next entry from history
            view.emit_stop_by_name("key_press_event")
            self.history_down()
            GObject.idle_add(self.scroll_to_end)
            return True

        elif keyname == "KP_Up" or keyname == "Up":
            # Previous entry from history
            view.emit_stop_by_name("key_press_event")
            self.history_up()
            GObject.idle_add(self.scroll_to_end)
            return True

        elif keyname == "KP_Left" or keyname == "Left" or \
             keyname == "BackSpace":
            inp = buffer.get_iter_at_mark(buffer.get_mark("input"))
            cur = buffer.get_iter_at_mark(buffer.get_insert())
            if inp.compare(cur) == 0:
                if not event_state:
                    buffer.place_cursor(inp)
                return True
            return False

        # For the console we enable smart/home end behavior incoditionally
        # since it is useful when editing python

        elif (keyname == "KP_Home" or keyname == "Home") and \
             event_state == event_state & (Gdk.ModifierType.SHIFT_MASK|Gdk.ModifierType.CONTROL_MASK):
            # Go to the begin of the command instead of the begin of the line
            iter = buffer.get_iter_at_mark(buffer.get_mark("input"))
            ins = buffer.get_iter_at_mark(buffer.get_insert())

            while iter.get_char().isspace():
                iter.forward_char()

            if iter.equal(ins):
                iter = buffer.get_iter_at_mark(buffer.get_mark("input"))

            if event_state & Gdk.ModifierType.SHIFT_MASK:
                buffer.move_mark_by_name("insert", iter)
            else:
                buffer.place_cursor(iter)
            return True

        elif (keyname == "KP_End" or keyname == "End") and \
             event_state == event_state & (Gdk.ModifierType.SHIFT_MASK|Gdk.ModifierType.CONTROL_MASK):

            iter = buffer.get_end_iter()
            ins = buffer.get_iter_at_mark(buffer.get_insert())

            iter.backward_char()

            while iter.get_char().isspace():
                iter.backward_char()

            iter.forward_char()

            if iter.equal(ins):
                iter = buffer.get_end_iter()

            if event_state & Gdk.ModifierType.SHIFT_MASK:
                buffer.move_mark_by_name("insert", iter)
            else:
                buffer.place_cursor(iter)
            return True

        return False

    def __mark_set_cb(self, buffer, iter, name):
        if self.mode != 'repl':
            return
        input = buffer.get_iter_at_mark(buffer.get_mark("input"))
        pos   = buffer.get_iter_at_mark(buffer.get_insert())
        self.view.set_editable(pos.compare(input) != -1)

    def get_command_line(self):
        buffer = self.view.get_buffer()
        inp = buffer.get_iter_at_mark(buffer.get_mark("input"))
        cur = buffer.get_end_iter()
        return buffer.get_text(inp, cur, False)

    def set_command_line(self, command):
        buffer = self.view.get_buffer()
        mark = buffer.get_mark("input")
        inp = buffer.get_iter_at_mark(mark)
        cur = buffer.get_end_iter()
        buffer.delete(inp, cur)
        buffer.insert(inp, command)
        self.view.grab_focus()

    def history_add(self, line):
        if line.strip() != '':
            self.history_pos = len(self.history)
            self.history[self.history_pos - 1] = line
            self.history.append('')

    def history_up(self):
        if self.history_pos > 0:
            self.history[self.history_pos] = self.get_command_line()
            self.history_pos = self.history_pos - 1
            self.set_command_line(self.history[self.history_pos])

    def history_down(self):
        if self.history_pos < len(self.history) - 1:
            self.history[self.history_pos] = self.get_command_line()
            self.history_pos = self.history_pos + 1
            self.set_command_line(self.history[self.history_pos])

    def __append_code_buffer(self, code):
        if not code:
            return

        if self.code_buffer and not self.code_buffer.endswith('\n'):
            self.code_buffer += '\n'

        self.code_buffer += code

        if not self.code_buffer.endswith('\n'):
            self.code_buffer += '\n'

    def scroll_to_end(self):
        iter = self.view.get_buffer().get_end_iter()
        self.view.scroll_to_iter(iter, 0.0, False, 0.5, 0.5)
        return False

    def write(self, text, tag = None):
        buffer = self.view.get_buffer()
        if tag is None:
            buffer.insert(buffer.get_end_iter(), text)
        else:
            buffer.insert_with_tags(buffer.get_end_iter(), text, tag)
        GObject.idle_add(self.scroll_to_end)

    def eval(self, command, display_command = False):
        buffer = self.view.get_buffer()
        lin = buffer.get_mark("input-line")
        buffer.delete(buffer.get_iter_at_mark(lin),
                      buffer.get_end_iter())

        if isinstance(command, list) or isinstance(command, tuple):
            for c in command:
                if display_command:
                    self.write(">>> " + c + "\n", self.command)
                self.__run(c)
        else:
            if display_command:
                self.write(">>> " + command + "\n", self.command)
            self.__run(command)

        cur = buffer.get_end_iter()
        buffer.move_mark_by_name("input-line", cur)
        buffer.insert(cur, ">>> ")
        cur = buffer.get_end_iter()
        buffer.move_mark_by_name("input", cur)
        self.view.scroll_to_iter(buffer.get_end_iter(), 0.0, False, 0.5, 0.5)

    def __run(self, command):
        sys.stdout, self.stdout = self.stdout, sys.stdout
        sys.stderr, self.stderr = self.stderr, sys.stderr

        # Decide eval-vs-exec *before* the try/except that actually runs the
        # code. Compiling and running inside the same except SyntaxError
        # block would make Python chain a real runtime error (e.g. a
        # NameError from exec) onto that already-handled SyntaxError,
        # printing a second, internal-looking "During handling of the
        # above exception..." traceback rooted at this very function.
        try:
            code_obj = compile(command, '<console>', 'eval')
            run_mode = 'eval'
        except SyntaxError:
            run_mode = 'exec'

        try:
            if run_mode == 'eval':
                r = eval(code_obj, self.namespace, self.namespace)
                if r is not None:
                    print(repr(r))
            else:
                code_obj = compile(command, '<console>', 'exec')
                exec(code_obj, self.namespace)
        except:
            if hasattr(sys, 'last_type') and sys.last_type == SystemExit:
                self.destroy()
            else:
                self.__print_clean_traceback()

        sys.stdout, self.stdout = self.stdout, sys.stdout
        sys.stderr, self.stderr = self.stderr, sys.stderr

    def __print_clean_traceback(self):
        """Print the current exception without this module's own frames,
        so users only see frames from the code they actually typed - the
        same thing a real interactive interpreter would show."""
        exc_type, exc_value, exc_tb = sys.exc_info()
        tb = exc_tb
        while tb is not None and tb.tb_frame.f_code.co_filename == __file__:
            tb = tb.tb_next
        traceback.print_exception(exc_type, exc_value, tb)

    # -- Internal terminal commands ------------------------------------

    def dispatch_command(self, name, arg):
        if self.mode == 'edit':
            self.__sync_code_buffer()

        if name == 'help':
            self.__finish(HELP_TEXT, self.normal)
        elif name == 'clear':
            self.__cmd_clear()
        elif name == 'list':
            self.__cmd_list()
        elif name == 'new':
            self.__cmd_new()
        elif name == 'run':
            self.__cmd_run()
        elif name == 'edit':
            self.__cmd_edit()
        elif name == 'repl':
            self.__cmd_repl()
        elif name == 'mode':
            self.__finish("%s\n" % self.mode, self.normal)
        elif name == 'save':
            self.__cmd_save(arg)
        elif name == 'load':
            self.__cmd_load(arg)
        elif name == 'quit':
            self.destroy()

    def __sorted_output_ranges(self):
        buffer = self.view.get_buffer()
        ranges = list(self.__output_ranges)

        def range_offset(output_range):
            mark = output_range.get('start_mark')
            if mark is None:
                return -1
            return buffer.get_iter_at_mark(mark).get_offset()

        ranges.sort(key=range_offset)
        return ranges

    def __collect_code_buffer(self, limit_iter = None):
        buffer = self.view.get_buffer()
        start_mark = buffer.get_mark("buffer-start")
        end_mark = buffer.get_mark("buffer-end")
        start = buffer.get_iter_at_mark(start_mark) if start_mark else buffer.get_start_iter()
        finish = limit_iter or (buffer.get_iter_at_mark(end_mark) if end_mark else buffer.get_end_iter())
        chunks = []
        cursor = start

        for output_range in self.__sorted_output_ranges():
            start_mark = output_range.get('start_mark')
            end_mark = output_range.get('end_mark')
            output_start = buffer.get_iter_at_mark(start_mark) if start_mark else None
            output_end = buffer.get_iter_at_mark(end_mark) if end_mark else None
            if output_start is not None and output_end is not None and \
               output_end.compare(start) > 0 and output_start.compare(finish) < 0:
                if output_start.compare(cursor) > 0:
                    chunks.append(buffer.get_text(cursor, output_start, False))
                if output_end.compare(cursor) > 0:
                    cursor = output_end

        if cursor.compare(finish) < 0:
            chunks.append(buffer.get_text(cursor, finish, False))

        visible = ''.join(chunks)
        if self.__hidden_prefix and visible and not self.__hidden_prefix.endswith('\n'):
            return self.__hidden_prefix + '\n' + visible
        return self.__hidden_prefix + visible

    def __sync_code_buffer(self):
        self.code_buffer = self.__collect_code_buffer()

    def __mark_code_region(self, start, end, end_left_gravity = False):
        buffer = self.view.get_buffer()
        start_mark = buffer.get_mark("buffer-start")
        end_mark = buffer.get_mark("buffer-end")

        if start_mark is None:
            buffer.create_mark("buffer-start", start, True)
        else:
            buffer.move_mark(start_mark, start)

        if end_mark is not None:
            buffer.delete_mark(end_mark)
        buffer.create_mark("buffer-end", end, end_left_gravity)

    def __clear_output_ranges(self):
        buffer = self.view.get_buffer()
        for output_range in self.__output_ranges:
            start_mark = output_range.get('start_mark')
            end_mark = output_range.get('end_mark')
            if start_mark is not None:
                buffer.delete_mark(start_mark)
            if end_mark is not None:
                buffer.delete_mark(end_mark)
        self.__output_ranges = []
        self.__output_range_counter = 0

    def __add_output_range(self, start, end):
        buffer = self.view.get_buffer()
        self.__output_range_counter += 1
        index = self.__output_range_counter
        start_mark = buffer.create_mark("output-start-%d" % index, start, False)
        end_mark = buffer.create_mark("output-end-%d" % index, end, True)
        self.__output_ranges.append({
            'start_mark': start_mark,
            'end_mark': end_mark,
        })

    def __cmd_clear(self):
        buffer = self.view.get_buffer()
        if self.mode == 'edit':
            # "clear" wipes the screen only - the buffer (already fresh
            # via the sync at the top of dispatch_command) is kept, just
            # not shown, until "list" (or any other command, which all
            # flatten this back via __finish_edit) redisplays it.
            self.__hidden_prefix = self.code_buffer
            self.__clear_output_ranges()
            buffer.set_text('')
            end = buffer.get_end_iter()
            self.__mark_code_region(end, end)
            buffer.place_cursor(end)
            self.view.set_editable(True)
            self.view.scroll_to_iter(end, 0.0, False, 0.5, 0.5)
        else:
            buffer.set_text('')
            self.__finish_repl()

    def __cmd_list(self):
        if self.mode == 'edit':
            self.__finish_edit()
        else:
            text = self.code_buffer if self.code_buffer else '(empty)\n'
            if not text.endswith('\n'):
                text += '\n'
            self.__finish_repl(text, self.normal)

    def __cmd_new(self):
        self.code_buffer = ''
        if self.mode == 'edit':
            self.__finish_edit()
        else:
            self.__finish_repl('Buffer cleared.\n', self.normal)

    def __cmd_run(self):
        pending_run_code = self.__pending_run_code
        self.__pending_run_code = None

        code = pending_run_code or self.code_buffer
        display_code = self.code_buffer
        if self.mode == 'edit':
            self.__finish_run_edit(code, display_code)
        else:
            self.__run(code)
            self.__finish_repl()

    def __finish_run_edit(self, code, display_code = None):
        """Show "run"'s output below the code, not above it - unlike
        every other edit-mode command. The output range is marked so it
        stays visible/editable but is ignored when syncing code."""
        if display_code is None:
            display_code = code

        buffer = self.view.get_buffer()
        self.__clear_output_ranges()
        buffer.set_text('')
        code_start_offset = buffer.get_end_iter().get_offset()
        self.write(code)
        added_separator = code and not code.endswith('\n')
        if added_separator:
            self.write('\n')
        output_start_offset = buffer.get_end_iter().get_offset()
        self.__run(code)
        output_end_offset = buffer.get_end_iter().get_offset()
        self.__hidden_prefix = ''

        if output_end_offset > output_start_offset:
            self.__add_output_range(buffer.get_iter_at_offset(output_start_offset),
                                    buffer.get_iter_at_offset(output_end_offset))

        if display_code.startswith(code):
            suffix = display_code[len(code):]
            if added_separator and suffix.startswith('\n'):
                suffix = suffix[1:]
            if suffix:
                self.write(suffix)

        end = buffer.get_end_iter()
        self.__mark_code_region(buffer.get_iter_at_offset(code_start_offset), end)
        buffer.place_cursor(end)
        self.view.set_editable(True)
        self.view.scroll_to_iter(end, 0.0, False, 0.5, 0.5)

    def __cmd_edit(self):
        self.mode = 'edit'
        self.current_command = ''
        self.block_command = False
        self.__finish_edit()

    def __cmd_repl(self):
        self.mode = 'repl'
        self.current_command = ''
        self.block_command = False
        self.view.get_buffer().set_text('')
        self.__finish_repl()

    def __cmd_save(self, arg):
        if not arg:
            self.__finish('Usage: save <file>\n', self.error)
            return
        path = os.path.expanduser(arg)
        try:
            with open(path, 'w') as f:
                f.write(self.code_buffer)
            self.__finish('Saved to %s\n' % path, self.normal)
        except IOError as e:
            self.__finish('Could not save %s: %s\n' % (path, e), self.error)

    def __cmd_load(self, arg):
        if not arg:
            self.__finish('Usage: load <file>\n', self.error)
            return
        path = os.path.expanduser(arg)
        try:
            with open(path, 'r') as f:
                self.code_buffer = f.read()
            self.__finish('Loaded %s\n' % path, self.normal)
        except IOError as e:
            self.__finish('Could not load %s: %s\n' % (path, e), self.error)

    def __finish(self, message = None, tag = None):
        if self.mode == 'edit':
            self.__finish_edit(message, tag)
        else:
            self.__finish_repl(message, tag)

    def __finish_repl(self, message = None, tag = None):
        buffer = self.view.get_buffer()
        if message:
            self.write(message, tag)
        cur = buffer.get_end_iter()
        buffer.move_mark_by_name("input-line", cur)
        buffer.insert(cur, ">>> ")
        cur = buffer.get_end_iter()
        buffer.move_mark_by_name("input", cur)
        buffer.place_cursor(cur)
        self.view.set_editable(True)
        self.view.scroll_to_iter(buffer.get_end_iter(), 0.0, False, 0.5, 0.5)

    def __finish_edit(self, message = None, tag = None, cleared = False, output_fn = None):
        # Every command other than "clear" shows the buffer in full, so
        # nothing stays hidden past this point (self.code_buffer already
        # has any hidden prefix folded in, from the sync at the top of
        # dispatch_command).
        self.__hidden_prefix = ''
        self.__clear_output_ranges()
        buffer = self.view.get_buffer()
        if not cleared:
            buffer.set_text('')
        if message:
            self.write(message, tag)
        if output_fn:
            output_fn()
        # Everything above this point (help text, run output, load/save
        # confirmations...) is a one-off message, not code. Mark where the
        # real editable buffer starts so a later sync doesn't fold that
        # message back into the saved/run buffer.
        start_offset = buffer.get_end_iter().get_offset()
        self.write(self.code_buffer)
        self.__mark_code_region(buffer.get_iter_at_offset(start_offset),
                                buffer.get_end_iter())
        cur = buffer.get_end_iter()
        buffer.place_cursor(cur)
        self.view.set_editable(True)
        self.view.scroll_to_iter(cur, 0.0, False, 0.5, 0.5)

    def __show_repl_prompt(self, com_mark):
        buffer = self.view.get_buffer()
        lin_mark = buffer.get_mark("input-line")
        inp_mark = buffer.get_mark("input")
        cur = buffer.get_end_iter()
        buffer.move_mark(lin_mark, cur)
        buffer.insert(cur, com_mark)
        cur = buffer.get_end_iter()
        buffer.move_mark(inp_mark, cur)
        buffer.place_cursor(cur)

    def destroy(self):
        # "Quit" (and Ctrl+D) closes the terminal - but if we're in edit
        # mode, that nested mode is what should be exited first, same as
        # typing "repl" would; only actually hide the console once we're
        # already back at the top-level repl.
        if self.mode == 'edit':
            self.__sync_code_buffer()
            self.__cmd_repl()
            return

        # Deferred: this is normally called from inside the very
        # key-press-event handler of a widget that lives inside the
        # bottom panel. Hiding an ancestor of the widget whose signal is
        # still being dispatched confuses GTK's focus/event bookkeeping
        # and left the console unresponsive after the panel was reopened.
        # Doing it on the next main-loop iteration avoids that.
        if self._window is not None:
            panel = self._window.get_bottom_panel()

            def _close():
                panel.hide()
                # Reopening should start a fresh session, not resume
                # wherever this one left off.
                self.__reset()
                return False

            GObject.idle_add(_close)

class OutFile:
    """A fake output file object. It sends output to a TK test widget,
    and if asked for a file number, returns one set on instance creation"""
    def __init__(self, console, fn, tag):
        self.fn = fn
        self.console = console
        self.tag = tag
    def close(self):         pass
    def flush(self):         pass
    def fileno(self):        return self.fn
    def isatty(self):        return 0
    def read(self, a):       return ''
    def readline(self):      return ''
    def readlines(self):     return []
    def write(self, s):      self.console.write(s, self.tag)
    def writelines(self, l): self.console.write(l, self.tag)
    def seek(self, a):       raise IOError(29, 'Illegal seek')
    def tell(self):          raise IOError(29, 'Illegal seek')
    truncate = tell

# ex:et:ts=4:
