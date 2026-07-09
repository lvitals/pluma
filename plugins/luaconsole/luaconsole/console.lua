--[[
  console.lua -- Interactive Lua console widget.

  Structurally mirrors the pythonconsole plugin's console.py (same
  TextView-based terminal, history, key bindings, repl/edit modes and
  internal command set), but evaluation uses Lua's own compiler/pcall
  instead of Python's code module. By default it runs the system "lua"
  command, and preferences can point evaluation at another Lua executable
  or at the embedded interpreter used by the plugin loader.
]]

local lgi = require('luaconsole.compat')
local GLib = lgi.GLib
local Gdk = lgi.Gdk
local Gtk = lgi.Gtk
local Pango = lgi.Pango

local Config = require('luaconsole.config').Config
local Engine = require('luaconsole.engine')

local DEFAULT_FONT = 'Monospace 10'

-- Commands recognized by the terminal regardless of the current mode. They
-- take precedence over language evaluation, so e.g. typing the bare word
-- "list" always shows the buffer instead of evaluating a global named list.
local BARE_COMMANDS = {
    help = true, clear = true, list = true, new = true, run = true,
    edit = true, repl = true, mode = true, quit = true,
}

local HELP_TEXT = table.concat({
    'Available commands:',
    '  help          show this list of commands',
    '  clear         clear the screen (the code buffer is kept)',
    '  list          show the code currently stored in the buffer',
    '  new           clear the code buffer',
    '  run           run all the code stored in the buffer',
    '  edit          switch to buffer edit mode',
    '  repl          switch to immediate execution mode',
    '  mode          show the current mode',
    '  save <file>   save the buffer to a file',
    '  load <file>   load a file into the buffer',
    '  quit          close the console',
    '',
}, '\n')

local function match_command(stripped)
    if BARE_COMMANDS[stripped] then
        return stripped, nil
    end
    local name, arg = stripped:match('^(save)%s+(%S+)$')
    if name then
        return name, arg
    end
    name, arg = stripped:match('^(load)%s+(%S+)$')
    if name then
        return name, arg
    end
    return nil
end

-- Lua strings are raw bytes, not validated Unicode. The parser's own
-- error messages can quote a non-ASCII token by byte, e.g. an accented
-- character split at a "near '<partial char>'" boundary - which produces
-- a truncated, invalid UTF-8 sequence. GtkTextBuffer requires valid
-- UTF-8 and hits a hard assertion failure otherwise (reproduced: typing
-- "print(Olá)" - not valid Lua, since bare identifiers are ASCII-only -
-- crashed gtk_text_buffer_emit_insert). Replace any invalid byte with
-- U+FFFD before this (or anything else) ever reaches buffer:insert().
local function sanitize_utf8(s)
    local out, i, n = {}, 1, #s
    while i <= n do
        local b1 = s:byte(i)
        local len
        if b1 < 0x80 then
            len = 1
        elseif b1 >= 0xC2 and b1 <= 0xDF then
            len = 2
        elseif b1 >= 0xE0 and b1 <= 0xEF then
            len = 3
        elseif b1 >= 0xF0 and b1 <= 0xF4 then
            len = 4
        else
            len = 0
        end

        local valid = len > 0 and i + len - 1 <= n
        if valid then
            for j = 1, len - 1 do
                local bx = s:byte(i + j)
                if not bx or bx < 0x80 or bx > 0xBF then
                    valid = false
                    break
                end
            end
        end

        if valid then
            out[#out + 1] = s:sub(i, i + len - 1)
            i = i + len
        else
            out[#out + 1] = '\239\191\189' -- U+FFFD, UTF-8 encoded
            i = i + 1
        end
    end
    return table.concat(out)
end

local Console = {}
Console.__index = Console

function Console.new(namespace)
    local self = setmetatable({}, Console)

    self.widget = Gtk.ScrolledWindow {
        hscrollbar_policy = 'NEVER',
        vscrollbar_policy = 'AUTOMATIC',
        shadow_type = 'IN',
    }
    self.view = Gtk.TextView {
        editable = true,
        wrap_mode = 'WORD_CHAR',
    }
    self.widget:add(self.view)
    self.view:show()

    -- GtkScrolledWindow has no text of its own to focus; forward focus to
    -- the inner view whenever the panel activates this widget.
    self.widget.on_grab_focus = function()
        self.view:grab_focus()
        -- Reopening the console (e.g. after "quit" hid the bottom panel)
        -- must not land on a stale cursor position: if the caret was left
        -- somewhere the repl mode considers read-only, set_editable()
        -- would otherwise stick at false with no further mark-set event
        -- to correct it, making the console look frozen.
        self.buffer:place_cursor(self.buffer:get_end_iter())
        self.view:set_editable(true)
    end

    local buffer = self.view:get_buffer()
    self.buffer = buffer

    -- GtkTextBuffer.create_tag() is a C varargs function; lgi has no
    -- marshaller for those and can't call it directly. Build the tag
    -- and register it with the buffer's tag table instead, which is
    -- the plain (non-varargs) equivalent.
    local tag_table = buffer:get_tag_table()
    self.normal_tag = Gtk.TextTag.new('normal')
    tag_table:add(self.normal_tag)
    self.error_tag = Gtk.TextTag.new('error')
    tag_table:add(self.error_tag)
    self.command_tag = Gtk.TextTag.new('command')
    tag_table:add(self.command_tag)

    namespace = namespace or {}
    setmetatable(namespace, { __index = _G })
    self.namespace = namespace
    self.window = namespace.window

    self.config = Config.new()
    self.config:add_handler(function() self:apply_preferences() end)
    self:apply_preferences()

    -- print() would otherwise write to the real process stdout (the
    -- terminal pluma was launched from, if any), invisible from inside
    -- the editor. Route it into the console buffer instead, like
    -- everything else typed here.
    namespace.print = function(...)
        local parts = {}
        for i = 1, select('#', ...) do
            parts[i] = tostring((select(i, ...)))
        end
        self:write(table.concat(parts, '\t') .. '\n', self.normal_tag)
    end

    self:reset()

    self.view.on_key_press_event = function(view, event)
        return self:on_key_press_event(view, event)
    end
    buffer.on_mark_set = function(buf, iter, mark)
        self:on_mark_set(buf, iter, mark)
    end

    return self
end

-- Puts the console back to its just-opened state: repl mode, an empty
-- code buffer, no history, and a fresh version/help banner. Used both at
-- construction and after "quit" actually closes the console, so
-- reopening it starts a clean session rather than resuming wherever the
-- last one left off.
function Console:reset()
    self.history = { '' }
    self.history_pos = 1
    self.current_command = ''
    self.block_command = false

    -- Terminal mode: 'repl' runs each line/block as soon as it is
    -- entered; 'edit' stores everything typed into self.code_buffer as
    -- plain text instead, for later "run"/"save".
    self.mode = 'repl'
    self.code_buffer = ''
    -- Code that "clear" hid from the screen but that's still part of
    -- the buffer; see sync_code_buffer/cmd_clear.
    self.hidden_prefix = ''

    local buffer = self.buffer
    buffer:set_text('', -1)

    -- Startup banner: selected interpreter version, then how to get help.
    local version_line = self.engine:get_version() .. '\n'
    local help_line = "Type 'help' to list available commands and descriptions.\n"
    buffer:insert(buffer:get_end_iter(), version_line .. help_line, -1)

    local end_iter = buffer:get_end_iter()
    if self.input_line_mark then
        buffer:move_mark(self.input_line_mark, end_iter)
    else
        self.input_line_mark = buffer:create_mark('input-line', end_iter, true)
    end

    buffer:insert(buffer:get_end_iter(), '> ', -1)
    end_iter = buffer:get_end_iter()
    if self.input_mark then
        buffer:move_mark(self.input_mark, end_iter)
    else
        self.input_mark = buffer:create_mark('input', end_iter, true)
    end

    self.view:set_editable(true)
end

function Console:apply_preferences()
    -- 'foreground' is a write-only GtkTextTag property (paired with
    -- 'foreground-set'); the generic set_property() wants a pre-boxed
    -- GObject.Value, but lgi's attribute-style assignment boxes it for us.
    self.error_tag.foreground = self.config:get_color_error()
    self.command_tag.foreground = self.config:get_color_command()

    local font_name
    if self.config:get_use_system_font() then
        font_name = self.config:get_monospace_font_name()
    else
        font_name = self.config:get_font()
    end

    local font_desc = Pango.FontDescription.from_string(font_name or DEFAULT_FONT)
    self.view:override_font(font_desc)

    local old_interpreter = self.lua_interpreter
    self.lua_interpreter = self.config:get_lua_interpreter()
    self.engine = Engine.new(self.lua_interpreter, self.namespace)

    if old_interpreter ~= nil and old_interpreter ~= self.lua_interpreter then
        self.engine:reset()
        self:reset()
    end
end

function Console:stop()
    self.namespace = nil
end

function Console:match_command(stripped)
    return match_command(stripped)
end

function Console:get_command_line()
    local buffer = self.buffer
    local inp = buffer:get_iter_at_mark(buffer:get_mark('input'))
    local cur = buffer:get_end_iter()
    return buffer:get_text(inp, cur, false)
end

function Console:set_command_line(command)
    local buffer = self.buffer
    local mark = buffer:get_mark('input')
    local inp = buffer:get_iter_at_mark(mark)
    local cur = buffer:get_end_iter()
    buffer:delete(inp, cur)
    buffer:insert(buffer:get_iter_at_mark(mark), command, -1)
    self.view:grab_focus()
end

function Console:history_add(line)
    if line:match('%S') then
        self.history_pos = #self.history + 1
        self.history[self.history_pos] = line
        self.history[self.history_pos + 1] = ''
    end
end

function Console:history_up()
    if self.history_pos > 1 then
        self.history[self.history_pos] = self:get_command_line()
        self.history_pos = self.history_pos - 1
        self:set_command_line(self.history[self.history_pos])
    end
end

function Console:history_down()
    if self.history_pos < #self.history then
        self.history[self.history_pos] = self:get_command_line()
        self.history_pos = self.history_pos + 1
        self:set_command_line(self.history[self.history_pos])
    end
end

function Console:scroll_to_end()
    local iter = self.buffer:get_end_iter()
    self.view:scroll_to_iter(iter, 0.0, false, 0.5, 0.5)
    return false
end

function Console:write(text, tag)
    local buffer = self.buffer
    text = sanitize_utf8(text)
    if tag then
        -- GtkTextBuffer.insert_with_tags() is also a C varargs function
        -- (see the create_tag note above); insert plain, then tag the
        -- inserted range.
        local start_offset = buffer:get_end_iter():get_offset()
        buffer:insert(buffer:get_end_iter(), text, -1)
        buffer:apply_tag(tag, buffer:get_iter_at_offset(start_offset), buffer:get_end_iter())
    else
        buffer:insert(buffer:get_end_iter(), text, -1)
    end
    GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
end

-- Compiles and runs one chunk (a single repl line/block, or the whole
-- code buffer for "run"). silent_on_incomplete lets the repl caller treat
-- an unfinished block as "need another line" instead of a real error.
function Console:eval_chunk(command, silent_on_incomplete, chunkname)
    local status, output, is_error = self.engine:execute(command, silent_on_incomplete, chunkname or '=console')

    if output and output ~= '' then
        self:write(output, is_error and self.error_tag or self.normal_tag)
    end

    return status
end

function Console:on_key_press_event(view, event)
    local keyname = Gdk.keyval_name(event.keyval)
    local buffer = view:get_buffer()

    if keyname == 'd' and event.state.CONTROL_MASK then
        self:destroy()
        return true
    end

    if self.mode == 'edit' then
        return self:on_edit_key_press_event(view, buffer, keyname, event)
    end
    return self:on_repl_key_press_event(view, buffer, keyname, event)
end

function Console:on_edit_key_press_event(view, buffer, keyname, event)
    if keyname == 'Return' then
        local end_iter = buffer:get_end_iter()
        local ins = buffer:get_iter_at_mark(buffer:get_insert())
        if ins:compare(end_iter) ~= 0 then
            -- Editing an earlier line: let GTK split it normally.
            return false
        end

        local line_start = buffer:get_iter_at_line(end_iter:get_line())
        local line = buffer:get_text(line_start, end_iter, false)
        local name, arg = match_command((line:gsub('^%s+', ''):gsub('%s+$', '')))
        if name then
            buffer:delete(line_start, buffer:get_end_iter())
            self:dispatch_command(name, arg)
        else
            buffer:insert(buffer:get_end_iter(), '\n', -1)
            buffer:place_cursor(buffer:get_end_iter())
            GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
        end
        return true
    end

    -- Everything else (navigation, backspace, home/end, up/down) uses
    -- plain GtkTextView behaviour so the whole buffer stays editable.
    return false
end

function Console:on_repl_key_press_event(view, buffer, keyname, event)
    local state = event.state
    local ctrl = state.CONTROL_MASK
    local shift = state.SHIFT_MASK
    local extra_mod = state.MOD1_MASK or state.SUPER_MASK

    if keyname == 'Return' and ctrl then
        local inp = buffer:get_iter_at_mark(buffer:get_mark('input'))
        local cur = buffer:get_end_iter()
        local line = buffer:get_text(inp, cur, false)
        self.current_command = self.current_command .. line .. '\n'
        self:history_add(line)

        buffer:insert(buffer:get_end_iter(), '\n.. ', -1)
        buffer:move_mark(buffer:get_mark('input'), buffer:get_end_iter())

        local leading = line:match('^%s+')
        if leading then
            buffer:insert(buffer:get_end_iter(), leading, -1)
        end

        buffer:place_cursor(buffer:get_end_iter())
        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
        return true

    elseif keyname == 'Return' then
        local lin_mark = buffer:get_mark('input-line')
        local inp_mark = buffer:get_mark('input')

        local inp = buffer:get_iter_at_mark(inp_mark)
        local cur = buffer:get_end_iter()
        local line = buffer:get_text(inp, cur, false)

        local lin = buffer:get_iter_at_mark(lin_mark)
        cur = buffer:get_end_iter()
        buffer:apply_tag(self.command_tag, lin, cur)
        buffer:insert(buffer:get_end_iter(), '\n', -1)

        local stripped = line:gsub('^%s+', ''):gsub('%s+$', '')
        -- Not "(cond) and match_command(stripped) or nil": that idiom
        -- truncates match_command's second return value (arg) to just
        -- the first (name) whenever cond is true, since and/or only
        -- pass a single value through - silently losing the filename for
        -- every "save"/"load" typed at the top-level prompt.
        local name, arg
        if not self.block_command then
            name, arg = match_command(stripped)
        end
        if name then
            self:history_add(line)
            self:dispatch_command(name, arg)
            GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
            return true
        end

        self.current_command = self.current_command .. line .. '\n'
        self:history_add(line)

        local status = self:eval_chunk(self.current_command, true)
        local prompt
        if status == 'incomplete' then
            self.block_command = true
            prompt = '.. '
        else
            self.current_command = ''
            self.block_command = false
            prompt = '> '
        end

        self:show_repl_prompt(prompt)
        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
        return true

    elseif keyname == 'KP_Down' or keyname == 'Down' then
        -- No emit_stop_by_name() here: LuaGObject doesn't bind it on
        -- Gtk.TextView ("Gtk.TextView: no `emit_stop_by_name'"), and it's
        -- unnecessary anyway - returning true below already stops this
        -- boolean-return event signal from reaching the default handler.
        self:history_down()
        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
        return true

    elseif keyname == 'KP_Up' or keyname == 'Up' then
        self:history_up()
        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
        return true

    elseif keyname == 'KP_Left' or keyname == 'Left' or keyname == 'BackSpace' then
        local inp = buffer:get_iter_at_mark(buffer:get_mark('input'))
        local cur = buffer:get_iter_at_mark(buffer:get_insert())
        if inp:compare(cur) == 0 then
            if not (ctrl or shift or extra_mod) then
                buffer:place_cursor(inp)
            end
            return true
        end
        return false

    elseif (keyname == 'KP_Home' or keyname == 'Home') and not extra_mod then
        local iter = buffer:get_iter_at_mark(buffer:get_mark('input'))
        local ins = buffer:get_iter_at_mark(buffer:get_insert())

        while iter:get_char():match('%s') do
            iter:forward_char()
        end

        if iter:equal(ins) then
            iter = buffer:get_iter_at_mark(buffer:get_mark('input'))
        end

        if shift then
            buffer:move_mark_by_name('insert', iter)
        else
            buffer:place_cursor(iter)
        end
        return true

    elseif (keyname == 'KP_End' or keyname == 'End') and not extra_mod then
        local iter = buffer:get_end_iter()
        local ins = buffer:get_iter_at_mark(buffer:get_insert())

        iter:backward_char()
        while iter:get_char():match('%s') do
            iter:backward_char()
        end
        iter:forward_char()

        if iter:equal(ins) then
            iter = buffer:get_end_iter()
        end

        if shift then
            buffer:move_mark_by_name('insert', iter)
        else
            buffer:place_cursor(iter)
        end
        return true
    end

    return false
end

function Console:on_mark_set(buffer, iter, mark)
    if self.mode ~= 'repl' then
        return
    end
    local input = buffer:get_iter_at_mark(buffer:get_mark('input'))
    local pos = buffer:get_iter_at_mark(buffer:get_insert())
    self.view:set_editable(pos:compare(input) ~= -1)
end

-- -- Internal terminal commands -----------------------------------------

function Console:dispatch_command(name, arg)
    if self.mode == 'edit' then
        self:sync_code_buffer()
    end

    if name == 'help' then
        self:finish(HELP_TEXT, self.normal_tag)
    elseif name == 'clear' then
        self:cmd_clear()
    elseif name == 'list' then
        self:cmd_list()
    elseif name == 'new' then
        self:cmd_new()
    elseif name == 'run' then
        self:cmd_run()
    elseif name == 'edit' then
        self:cmd_edit()
    elseif name == 'repl' then
        self:cmd_repl()
    elseif name == 'mode' then
        self:finish(self.mode .. '\n', self.normal_tag)
    elseif name == 'save' then
        self:cmd_save(arg)
    elseif name == 'load' then
        self:cmd_load(arg)
    elseif name == 'quit' then
        self:destroy()
    end
end

function Console:sync_code_buffer()
    local buffer = self.buffer
    local mark = buffer:get_mark('buffer-start')
    local start = mark and buffer:get_iter_at_mark(mark) or buffer:get_start_iter()
    local visible = buffer:get_text(start, buffer:get_end_iter(), false)
    self.code_buffer = self.hidden_prefix .. visible
end

function Console:cmd_clear()
    local buffer = self.buffer
    if self.mode == 'edit' then
        -- "clear" wipes the screen only - the buffer (already fresh via
        -- the sync at the top of dispatch_command) is kept, just not
        -- shown, until "list" (or any other command, which all flatten
        -- this back via finish_edit) redisplays it.
        self.hidden_prefix = self.code_buffer
        buffer:set_text('', -1)
        local end_iter = buffer:get_end_iter()
        local mark = buffer:get_mark('buffer-start')
        if mark then
            buffer:move_mark(mark, end_iter)
        else
            buffer:create_mark('buffer-start', end_iter, true)
        end
        buffer:place_cursor(end_iter)
        self.view:set_editable(true)
        self.view:scroll_to_iter(end_iter, 0.0, false, 0.5, 0.5)
    else
        buffer:set_text('', -1)
        self:finish_repl()
    end
end

function Console:cmd_list()
    if self.mode == 'edit' then
        self:finish_edit()
    else
        local text = (self.code_buffer ~= '') and self.code_buffer or '(empty)\n'
        if text:sub(-1) ~= '\n' then
            text = text .. '\n'
        end
        self:finish_repl(text, self.normal_tag)
    end
end

function Console:cmd_new()
    self.code_buffer = ''
    if self.mode == 'edit' then
        self:finish_edit()
    else
        self:finish_repl('Buffer cleared.\n', self.normal_tag)
    end
end

function Console:cmd_run()
    local code = self.code_buffer
    if self.mode == 'edit' then
        self:finish_run_edit(code)
    else
        self:eval_chunk(code, false, '=buffer')
        self:finish_repl()
    end
end

-- Shows "run"'s output below the code, not above it - unlike every
-- other edit-mode command. The code just shown becomes the hidden
-- prefix (same trick cmd_clear uses): a fresh "buffer-start" mark goes
-- right after this whole display, so if the user keeps typing without
-- invoking another command, that new text is appended to the program
-- rather than getting mixed into this one-off code+output display.
function Console:finish_run_edit(code)
    local buffer = self.buffer
    buffer:set_text('', -1)
    self:write(code)
    if code ~= '' and code:sub(-1) ~= '\n' then
        self:write('\n')
    end
    self:eval_chunk(code, false, '=buffer')
    self.hidden_prefix = code

    local end_iter = buffer:get_end_iter()
    local mark = buffer:get_mark('buffer-start')
    if mark then
        buffer:move_mark(mark, end_iter)
    else
        buffer:create_mark('buffer-start', end_iter, true)
    end
    buffer:place_cursor(end_iter)
    self.view:set_editable(true)
    self.view:scroll_to_iter(end_iter, 0.0, false, 0.5, 0.5)
end

function Console:cmd_edit()
    self.mode = 'edit'
    self.current_command = ''
    self.block_command = false
    self:finish_edit()
end

function Console:cmd_repl()
    self.mode = 'repl'
    self.current_command = ''
    self.block_command = false
    self.buffer:set_text('', -1)
    self:finish_repl()
end

local function expand_home(path)
    if path:sub(1, 1) == '~' then
        local home = os.getenv('HOME')
        if home then
            return home .. path:sub(2)
        end
    end
    return path
end

function Console:cmd_save(arg)
    if not arg then
        self:finish('Usage: save <file>\n', self.error_tag)
        return
    end
    local path = expand_home(arg)
    local f, err = io.open(path, 'w')
    if not f then
        self:finish(('Could not save %s: %s\n'):format(path, err), self.error_tag)
        return
    end
    f:write(self.code_buffer)
    f:close()
    self:finish(('Saved to %s\n'):format(path), self.normal_tag)
end

function Console:cmd_load(arg)
    if not arg then
        self:finish('Usage: load <file>\n', self.error_tag)
        return
    end
    local path = expand_home(arg)
    local f, err = io.open(path, 'r')
    if not f then
        self:finish(('Could not load %s: %s\n'):format(path, err), self.error_tag)
        return
    end
    self.code_buffer = f:read('*a')
    f:close()
    self:finish(('Loaded %s\n'):format(path), self.normal_tag)
end

function Console:finish(message, tag)
    if self.mode == 'edit' then
        self:finish_edit(message, tag)
    else
        self:finish_repl(message, tag)
    end
end

function Console:finish_repl(message, tag)
    local buffer = self.buffer
    if message then
        self:write(message, tag)
    end
    local cur = buffer:get_end_iter()
    buffer:move_mark_by_name('input-line', cur)
    buffer:insert(buffer:get_end_iter(), '> ', -1)
    cur = buffer:get_end_iter()
    buffer:move_mark_by_name('input', cur)
    buffer:place_cursor(cur)
    self.view:set_editable(true)
    self.view:scroll_to_iter(buffer:get_end_iter(), 0.0, false, 0.5, 0.5)
end

function Console:finish_edit(message, tag, cleared, output_fn)
    -- Every command other than "clear" shows the buffer in full, so
    -- nothing stays hidden past this point (self.code_buffer already
    -- has any hidden prefix folded in, from the sync at the top of
    -- dispatch_command).
    self.hidden_prefix = ''
    local buffer = self.buffer
    if not cleared then
        buffer:set_text('', -1)
    end
    if message then
        self:write(message, tag)
    end
    if output_fn then
        output_fn()
    end
    -- Everything above this point (help text, run output, load/save
    -- confirmations...) is a one-off message, not code. Mark where the
    -- real editable buffer starts so a later sync doesn't fold that
    -- message back into the saved/run buffer.
    local start = buffer:get_end_iter()
    local mark = buffer:get_mark('buffer-start')
    if mark then
        buffer:move_mark(mark, start)
    else
        buffer:create_mark('buffer-start', start, true)
    end
    self:write(self.code_buffer)
    local cur = buffer:get_end_iter()
    buffer:place_cursor(cur)
    self.view:set_editable(true)
    self.view:scroll_to_iter(cur, 0.0, false, 0.5, 0.5)
end

function Console:show_repl_prompt(prompt)
    local buffer = self.buffer
    local lin_mark = buffer:get_mark('input-line')
    local inp_mark = buffer:get_mark('input')
    local cur = buffer:get_end_iter()
    buffer:move_mark(lin_mark, cur)
    buffer:insert(buffer:get_end_iter(), prompt, -1)
    cur = buffer:get_end_iter()
    buffer:move_mark(inp_mark, cur)
    buffer:place_cursor(cur)
end

function Console:destroy()
    -- "Quit" (and Ctrl+D) closes the terminal - but if we're in edit
    -- mode, that nested mode is what should be exited first, same as
    -- typing "repl" would; only actually hide the console once we're
    -- already back at the top-level repl.
    if self.mode == 'edit' then
        self:sync_code_buffer()
        self:cmd_repl()
        return
    end

    -- Deferred: this is normally called from inside the very
    -- key-press-event handler of a widget that lives inside the bottom
    -- panel. Hiding an ancestor of the widget whose signal is still
    -- being dispatched confuses GTK's focus/event bookkeeping and left
    -- the console unresponsive after the panel was reopened. Doing it
    -- on the next main-loop iteration avoids that.
    if self.window then
        local panel = self.window:get_bottom_panel()
        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function()
            panel:hide()
            -- Reopening should start a fresh session, not resume
            -- wherever this one left off.
            self:reset()
            return false
        end)
    end
end

return Console
