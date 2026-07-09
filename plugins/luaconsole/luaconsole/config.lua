--[[
  config.lua -- Config dialog for the Lua console plugin, mirroring the
  pythonconsole plugin's config.py (same gsettings layout, ported to LGI).
]]

local lgi = require('luaconsole.compat')
local GLib = lgi.GLib
local Gio = lgi.Gio
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk

local CONSOLE_KEY_BASE = 'org.mate.pluma.plugins.luaconsole'
local INTERFACE_KEY_BASE = 'org.mate.interface'
local SYSTEM_INTERPRETER_ID = 'system'
local EMBEDDED_INTERPRETER_ID = 'embedded'

local console_settings = Gio.Settings.new(CONSOLE_KEY_BASE)
local interface_settings = Gio.Settings.new(INTERFACE_KEY_BASE)

local LUA_INTERPRETER_CANDIDATES = {
    'lua5.5', 'lua-5.5', 'lua55',
    'lua5.4', 'lua-5.4', 'lua54',
    'lua5.3', 'lua-5.3', 'lua53',
    'lua5.2', 'lua-5.2', 'lua52',
    'lua5.1', 'lua-5.1', 'lua51',
    'lua',
}

local interpreter_cache = {}

local Config = {}
Config.__index = Config

function Config.new()
    return setmetatable({}, Config)
end

function Config:get_color_command()
    return console_settings:get_string('command-color')
end

function Config:set_color_command(value)
    console_settings:set_string('command-color', value)
end

function Config:get_color_error()
    return console_settings:get_string('error-color')
end

function Config:set_color_error(value)
    console_settings:set_string('error-color', value)
end

function Config:get_use_system_font()
    return console_settings:get_boolean('use-system-font')
end

function Config:set_use_system_font(value)
    console_settings:set_boolean('use-system-font', value)
end

function Config:get_font()
    return console_settings:get_string('font')
end

function Config:set_font(value)
    console_settings:set_string('font', value)
end

function Config:get_lua_interpreter()
    return console_settings:get_string('lua-interpreter')
end

function Config:set_lua_interpreter(value)
    console_settings:set_string('lua-interpreter', value or '')
end

function Config:get_monospace_font_name()
    return interface_settings:get_string('monospace-font-name')
end

function Config:add_handler(handler)
    console_settings.on_changed = handler
    interface_settings.on_changed = handler
end

local ConfigWidget = {}
ConfigWidget.__index = ConfigWidget

function ConfigWidget.new(datadir)
    local self = setmetatable({}, ConfigWidget)
    self.widget = nil
    self.ui_path = datadir .. '/ui/config.ui'
    self.config = Config.new()
    self.builder = Gtk.Builder()
    return self
end

local function set_colorbutton_color(colorbutton, value)
    local rgba = Gdk.RGBA()
    if rgba:parse(value) then
        colorbutton:set_rgba(rgba)
    end
end

local function get_command_output(command)
    local pipe = io.popen(command .. ' 2>&1')
    if pipe == nil then
        return nil
    end

    local output = pipe:read('*a')
    pipe:close()
    output = (output or ''):gsub('[\r\n]+$', '')

    return output ~= '' and output or nil
end

local function shell_quote(value)
    return "'" .. tostring(value):gsub("'", "'\\''") .. "'"
end

local function get_lua_interpreter_info(path, executable)
    if interpreter_cache[path] ~= nil then
        return interpreter_cache[path]
    end

    local output = get_command_output(shell_quote(path) .. ' -v')
    local version = output and output:match('(Lua%s+%d+%.%d+%.?%d*)')

    if version == nil then
        version = executable
    end

    interpreter_cache[path] = {
        id = path,
        path = path,
        version = version,
        label = version,
    }

    return interpreter_cache[path]
end

local function find_lua_interpreters()
    local interpreters = {}
    local seen = {}

    for _, executable in ipairs(LUA_INTERPRETER_CANDIDATES) do
        local path = GLib.find_program_in_path(executable)

        if path ~= nil and seen[path] == nil then
            seen[path] = true
            interpreters[#interpreters + 1] = get_lua_interpreter_info(path, executable)
        end
    end

    return interpreters
end

local function embedded_lua_label()
    local version = (_VERSION or 'Lua'):match('Lua%s+(.+)')

    if version ~= nil then
        return 'Embedded Lua ' .. version
    end

    return 'Embedded Lua'
end

function ConfigWidget:on_colorbutton_command_color_set(colorbutton)
    self.config:set_color_command(colorbutton:get_color():to_string())
end

function ConfigWidget:on_colorbutton_error_color_set(colorbutton)
    self.config:set_color_error(colorbutton:get_color():to_string())
end

function ConfigWidget:on_checkbox_system_font_toggled(checkbox)
    local value = checkbox:get_active()
    self.config:set_use_system_font(value)
    self.fontbutton:set_sensitive(not value)
end

function ConfigWidget:on_fontbutton_font_set(fontbutton)
    self.config:set_font(fontbutton:get_font_name())
end

function ConfigWidget:on_combobox_lua_interpreter_changed(combobox)
    local active_id = combobox:get_active_id()

    if active_id ~= nil then
        if active_id == SYSTEM_INTERPRETER_ID then
            self.config:set_lua_interpreter('')
            combobox:set_tooltip_text(self.system_lua or 'lua')
        elseif active_id == EMBEDDED_INTERPRETER_ID then
            self.config:set_lua_interpreter(active_id)
            combobox:set_tooltip_text('Embedded Lua interpreter from the plugin loader')
        else
            self.config:set_lua_interpreter(active_id)
            combobox:set_tooltip_text(active_id)
        end
    end
end

function ConfigWidget:on_widget_config_parent_set(widget, old_parent)
    local ok, toplevel = pcall(function() return widget:get_toplevel() end)
    if not ok or toplevel == nil then
        return
    end

    local ok2, actionarea = pcall(function() return toplevel:get_action_area() end)
    if not ok2 or actionarea == nil then
        return
    end

    local image = Gtk.Image.new_from_icon_name('window-close-symbolic', Gtk.IconSize.BUTTON)
    for _, button in ipairs(actionarea:get_children()) do
        button:set_image(image)
        -- set_property() wants a pre-boxed GObject.Value; attribute-style
        -- assignment boxes it for us (see console.lua's apply_preferences).
        button.always_show_image = true
    end
end

function ConfigWidget:configure_widget()
    if self.widget == nil then
        self.builder:add_from_file(self.ui_path)

        set_colorbutton_color(self.builder:get_object('colorbutton-command'),
                               self.config:get_color_command())
        set_colorbutton_color(self.builder:get_object('colorbutton-error'),
                               self.config:get_color_error())

        local checkbox = self.builder:get_object('checkbox-system-font')
        checkbox:set_active(self.config:get_use_system_font())

        self.fontbutton = self.builder:get_object('fontbutton-font')
        self.fontbutton:set_font_name(self.config:get_font())
        self:on_checkbox_system_font_toggled(checkbox)

        self.lua_interpreter_combo = self.builder:get_object('combobox-lua-interpreter')
        local system_lua = GLib.find_program_in_path('lua')
        local system_label = 'System default Lua (lua)'
        self.system_lua = system_lua

        if system_lua ~= nil then
            system_label = get_lua_interpreter_info(system_lua, 'lua').label
        end

        self.lua_interpreter_combo:append(SYSTEM_INTERPRETER_ID, system_label)
        self.lua_interpreter_combo:append(EMBEDDED_INTERPRETER_ID, embedded_lua_label())

        for _, interpreter in ipairs(find_lua_interpreters()) do
            if interpreter.id ~= system_lua then
                self.lua_interpreter_combo:append(interpreter.id, interpreter.label)
            end
        end

        local configured_interpreter = self.config:get_lua_interpreter()
        if configured_interpreter == '' then
            configured_interpreter = SYSTEM_INTERPRETER_ID
        end

        if not self.lua_interpreter_combo:set_active_id(configured_interpreter) then
            self.lua_interpreter_combo:set_active_id(SYSTEM_INTERPRETER_ID)
            self.config:set_lua_interpreter('')
        end
        self:on_combobox_lua_interpreter_changed(self.lua_interpreter_combo)

        self.builder:connect_signals({
            on_colorbutton_command_color_set = function(...) return self:on_colorbutton_command_color_set(...) end,
            on_colorbutton_error_color_set = function(...) return self:on_colorbutton_error_color_set(...) end,
            on_checkbox_system_font_toggled = function(...) return self:on_checkbox_system_font_toggled(...) end,
            on_fontbutton_font_set = function(...) return self:on_fontbutton_font_set(...) end,
            on_combobox_lua_interpreter_changed = function(...) return self:on_combobox_lua_interpreter_changed(...) end,
            on_widget_config_parent_set = function(...) return self:on_widget_config_parent_set(...) end,
        })

        self.widget = self.builder:get_object('widget-config')
        self.widget:show_all()
    end

    return self.widget
end

return {
    Config = Config,
    ConfigWidget = ConfigWidget,
}
