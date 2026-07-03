--[[
  config.lua -- Config dialog for the Lua console plugin, mirroring the
  pythonconsole plugin's config.py (same gsettings layout, ported to LGI).
]]

local lgi = require('luaconsole.compat')
local Gio = lgi.Gio
local Gtk = lgi.Gtk
local Gdk = lgi.Gdk

local CONSOLE_KEY_BASE = 'org.mate.pluma.plugins.luaconsole'
local INTERFACE_KEY_BASE = 'org.mate.interface'

local console_settings = Gio.Settings.new(CONSOLE_KEY_BASE)
local interface_settings = Gio.Settings.new(INTERFACE_KEY_BASE)

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

function ConfigWidget:on_widget_config_parent_set(widget, old_parent)
    local ok, toplevel = pcall(function() return widget:get_toplevel() end)
    if not ok or toplevel == nil then
        return
    end

    local ok2, actionarea = pcall(function() return toplevel:get_action_area() end)
    if not ok2 or actionarea == nil then
        return
    end

    local image = Gtk.Image.new_from_icon_name('window-close', Gtk.IconSize.BUTTON)
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

        self.builder:connect_signals({
            on_colorbutton_command_color_set = function(...) return self:on_colorbutton_command_color_set(...) end,
            on_colorbutton_error_color_set = function(...) return self:on_colorbutton_error_color_set(...) end,
            on_checkbox_system_font_toggled = function(...) return self:on_checkbox_system_font_toggled(...) end,
            on_fontbutton_font_set = function(...) return self:on_fontbutton_font_set(...) end,
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
