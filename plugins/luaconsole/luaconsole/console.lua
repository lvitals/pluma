--[[
  console.lua -- Interactive Lua console widget.

  Structurally mirrors the pythonconsole plugin's console.py (same
  TextView-based REPL, history and key bindings), but evaluation uses Lua's
  own compiler/pcall instead of Python's code module. Targets Lua 5.1 (the
  version libpeas' native "lua5.1" loader embeds), so `loadstring`/`setfenv`
  are used instead of the 5.2+ `load(..., env)` form.
]]

local lgi = require('luaconsole.compat')
local GLib = lgi.GLib
local Gdk = lgi.Gdk
local Gtk = lgi.Gtk
local Pango = lgi.Pango

local Config = require('luaconsole.config').Config

local DEFAULT_FONT = 'Monospace 10'

-- Lua 5.1's parser reports an unfinished chunk (e.g. a dangling "if" or an
-- open string) with an error message ending in the quoted "<eof>" token.
-- That's the standard way lua.c itself detects "needs another line".
local function is_incomplete(err)
    return type(err) == 'string' and err:sub(-6) == "<eof>'"
end

local function compile_chunk(source, namespace)
    local chunk, err = loadstring('return ' .. source, '=console')
    if not chunk then
        chunk, err = loadstring(source, '=console')
    end
    if chunk then
        setfenv(chunk, namespace)
    end
    return chunk, err
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

    self.config = Config.new()
    self.config:add_handler(function() self:apply_preferences() end)
    self:apply_preferences()

    namespace = namespace or {}
    setmetatable(namespace, { __index = _G })
    self.namespace = namespace

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

    self.history = { '' }
    self.history_pos = 1
    self.current_command = ''
    self.block_command = false

    local end_iter = buffer:get_end_iter()
    self.input_line_mark = buffer:create_mark('input-line', end_iter, true)
    buffer:insert(buffer:get_end_iter(), '> ', -1)
    self.input_mark = buffer:create_mark('input', buffer:get_end_iter(), true)

    self.view.on_key_press_event = function(view, event)
        return self:on_key_press_event(view, event)
    end
    buffer.on_mark_set = function(buf, iter, mark)
        self:on_mark_set(buf, iter, mark)
    end

    return self
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
end

function Console:stop()
    self.namespace = nil
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

function Console:run(command)
    local chunk, err = compile_chunk(command, self.namespace)
    if not chunk then
        if is_incomplete(err) then
            return 'incomplete'
        end
        self:write(tostring(err) .. '\n', self.error_tag)
        return 'done'
    end

    local results = { pcall(chunk) }
    local ok = table.remove(results, 1)
    if not ok then
        self:write(tostring(results[1]) .. '\n', self.error_tag)
    elseif #results > 0 then
        local parts = {}
        for _, value in ipairs(results) do
            parts[#parts + 1] = tostring(value)
        end
        self:write(table.concat(parts, '\t') .. '\n', self.normal_tag)
    end
    return 'done'
end

function Console:on_key_press_event(view, event)
    local state = event.state
    local ctrl = state.CONTROL_MASK
    local shift = state.SHIFT_MASK
    local extra_mod = state.MOD1_MASK or state.SUPER_MASK
    local keyname = Gdk.keyval_name(event.keyval)
    local buffer = view:get_buffer()

    if keyname == 'd' and ctrl then
        self:destroy()
        return true

    elseif keyname == 'Return' and ctrl then
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
        self.current_command = self.current_command .. line .. '\n'
        self:history_add(line)

        local lin = buffer:get_iter_at_mark(lin_mark)
        cur = buffer:get_end_iter()
        buffer:apply_tag(self.command_tag, lin, cur)
        buffer:insert(buffer:get_end_iter(), '\n', -1)

        local status = self:run(self.current_command)
        local prompt
        if status == 'incomplete' then
            self.block_command = true
            prompt = '.. '
        else
            self.current_command = ''
            self.block_command = false
            prompt = '> '
        end

        cur = buffer:get_end_iter()
        buffer:move_mark(lin_mark, cur)
        buffer:insert(buffer:get_end_iter(), prompt, -1)
        cur = buffer:get_end_iter()
        buffer:move_mark(inp_mark, cur)
        buffer:place_cursor(cur)
        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
        return true

    elseif keyname == 'KP_Down' or keyname == 'Down' then
        view:emit_stop_by_name('key-press-event')
        self:history_down()
        GLib.idle_add(GLib.PRIORITY_DEFAULT_IDLE, function() return self:scroll_to_end() end)
        return true

    elseif keyname == 'KP_Up' or keyname == 'Up' then
        view:emit_stop_by_name('key-press-event')
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
    local input = buffer:get_iter_at_mark(buffer:get_mark('input'))
    local pos = buffer:get_iter_at_mark(buffer:get_insert())
    self.view:set_editable(pos:compare(input) ~= -1)
end

function Console:destroy()
end

return Console
